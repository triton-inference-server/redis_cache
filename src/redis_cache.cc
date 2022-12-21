// Copyright 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <sstream>
#include <cstdio>
#include <iterator>

#include "redis_cache.h"


std::unique_ptr<sw::redis::Redis> init_client(
    const std::string& address,
    const std::string& user_name,
    const std::string& password) {
  // Put together cluster configuration.
  sw::redis::ConnectionOptions options;

  const std::string::size_type comma_pos = address.find(',');
  const std::string host = comma_pos == std::string::npos ? address : address.substr(0, comma_pos);
  const std::string::size_type colon_pos = host.find(':');
  if (colon_pos == std::string::npos) {
    options.host = host;
  } else {
    options.host = host.substr(0, colon_pos);
    options.port = std::stoi(host.substr(colon_pos + 1));
  }
  options.user = user_name;
  options.password = password;
  options.keep_alive = true;

  sw::redis::ConnectionPoolOptions pool_options;
  pool_options.size = 1;

  // Connect to cluster.
  std::cout << "Connecting via " << options.host << ':' << options.port << "..." << std::endl;
  std::unique_ptr<sw::redis::Redis> redis = std::make_unique<sw::redis::Redis>(options, pool_options);
  return redis;
}


namespace triton { namespace cache { namespace core {

TRITONSERVER_Error*
RedisCache::Create(
    std::string address,
    std::string username,
    std::string password,
    std::unique_ptr<RedisCache>* cache)
{
  try {
    cache->reset(new RedisCache(address, username, password));
  }
  catch (const std::exception& ex) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        "Failed to initialize Response Cache: " + std::string(ex.what()));
  }
  return nullptr; //success
}

RedisCache::RedisCache(std::string address, std::string username, std::string password)
{

  try {
    this->_client = init_client(address, username, password);
  }
  catch (const std::exception& ex) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        "Failed to initialize Response Cache: " + std::string(ex.what()));
  }

  LOG_INFO << "Redis Response Cache is located at:" << address;
}

RedisCache::~RedisCache()
{
  this->_client.reset();
}

bool RedisCache::Exists(const uint64_t key) {
  std::string key_s = std::to_string(key);
  return this->_client->exists(key_s);
}

std::pair<TRITONSERVER_Error*, RedisCacheEntry>
RedisCache::Lookup(const std::string& key)
{

  num_lookups_++;
  LOG_VERBOSE(1) << request->LogRequest()
                 << "Looking up key [" + key + "] in cache.";

  // Search cache for request hash key
  bool found = this->Exists(key);

  if (!found) {
    num_misses_++;
    LOG_VERBOSE(1) << request->LogRequest()
                   << "MISS for key [" + key + "] in cache.";
    auto err = TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_NOT_FOUND,
        std::string("key [" + key + "] does not exist").c_str());
      return {err, {}};
  }

  RedisCacheEntry<std::string> entry;
  entry.key = key;
  RETURN_IF_ERROR(cache_get(&entry));

  // If find succeeds, it's a cache hit
  num_hits_++;
  LOG_VERBOSE(1) << request->LogRequest()
                 << "HIT for key [" + key + "] in cache.";

  return {nullptr, entry};
}

TRITONSERVER_Error*
RedisCache::Insert(const std::string& key, CacheEntry& entry)

  // Exit early if key already exists in cache
  // Search cache for request hash key
  bool found = this->Exists(key);

  if (!found) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_ALREADY_EXISTS,
        std::string("Failed to insert key [" + key + "] into map").c_str());
  }

  // Construct cache entry from response
  auto cache_entry = RedisCacheEntry<std::string>();
  cache_entry.key = key
  RETURN_IF_ERROR(cache_set(cache_entry));

  // Insert entry into cache
  LOG_VERBOSE(1) << request->LogRequest()
                 << "Inserting key [" + std::to_string(key) + "] into cache.";

  return nullptr; //success
}


TRITONSERVER_Error*
RedisCache::cache_set(RedisCacheEntry<std::string> &cache_entry) {

    // set number of entries in the top level
    const std::string& entries_k = "entries";
    const std::string& entries_v = std::to_string(cache_entry.num_entries);
    cache_entry.fields.insert({entries_k, entries_v});

    // set response in a redis hash field
    try {
      _client->hmset(
        cache_entry.key,
        cache_entry.fields.begin(),
        cache_entry.fields.end()
      );
    }
    catch (sw::redis::TimeoutError &e) {
      std::string err = "Timeout inserting key" + cache_entry.key + " into cache.";
      LOG_ERROR << err << "\n Failed with error " + std::string(e.what());
        return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());

  }
    catch (sw::redis::IoError &e) {
      std::string err = "Failed to insert key" + cache_entry.key + " into cache.";
      LOG_ERROR << err << "\n Failed with error " + std::string(e.what());
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());
      }
    catch (...) {
      std::string err = "Failed to insert key" + cache_entry.key + " into cache.";
      LOG_ERROR << err;
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());

    return nullptr; //success
  }



TRITONSERVER_Error*
RedisCache::cache_get(RedisCacheEntry<std::string> *cache_entry) {
    try {
      _client->hgetall(
        cache_entry->key,
        std::inserter(cache_entry->fields, cache_entry->fields.begin())
      );

    }
    catch (sw::redis::TimeoutError &e) {
      std::string err = "Timeout inserting key" + cache_entry->key + " into cache.";
      LOG_ERROR << err << "\n Failed with error " + std::string(e.what());
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());
    }
    catch (sw::redis::IoError &e) {
      std::string err = "Failed to insert key" + cache_entry->key + " into cache.";
      LOG_ERROR << err << "\n Failed with error " + std::string(e.what());
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());
    }
    catch (...) {
      std::string err = "Failed to insert key" + cache_entry->key + " into cache.";
      LOG_ERROR << err;
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());

    }

    // emptiness check
    if (cache_entry->fields.empty()) {
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string("failed to retrieve key " + cache_entry->key + " from cache").c_str());
    }

    // set number of entries at the top level
    const char* entries = cache_entry->fields.at("entries").c_str();
    cache_entry->num_entries = std::atoi(entries);

    return nullptr; //success
  }

}}}  // namespace triton::core
