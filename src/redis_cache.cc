// Copyright 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "rapidjson/document.h"


namespace triton { namespace cache { namespace redis {


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


TRITONSERVER_Error*
RedisCache::Create(
    const std::string& cache_config, std::unique_ptr<RedisCache>* cache)
  {
  rapidjson::Document document;

  document.Parse(cache_config.c_str());
  if (!document.HasMember("address")) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG,
        "Failed to initialize RedisCache, didn't specify address.");
  }
  std::string address = document["address"].GetString();
  std::string username = "default";
  std::string password = "";

  // set username and password if provided
  if (document.HasMember("username")) {
    username = document["username"].GetString();
  }
  if (document.HasMember("password")) {
    password = document["password"].GetString();
  }

  try {
    cache->reset(new RedisCache(address, username, password));
  }
  catch (const std::exception& ex) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        ("Failed to initialize Response Cache: " + std::string(ex.what())).c_str());
  }
  return nullptr; //success
}

// TODO: add support for all connection options
// https://github.com/sewenew/redis-plus-plus/blob/master/src/sw/redis%2B%2B/connection.h#L40
RedisCache::RedisCache(
  std::string address,
  std::string username,
  std::string password)
{

  try {
    this->_client = init_client(address, username, password);
  }
  catch (const std::exception& ex) {
    throw std::runtime_error(
      ("Failed to initialize Response Cache: " + std::string(ex.what())).c_str());
  }

}

RedisCache::~RedisCache()
{
  this->_client.reset();
}

TRITONSERVER_Error*
RedisCache::Allocate(uint64_t byte_size, void** buffer)
{
  // NOTE: Could have more fine-grained locking, or remove Evict()
  //       from this function and call separately
  std::unique_lock lk(buffer_mu_);

  // Requested buffer larger than total buffer
  if (byte_size > managed_buffer_.get_size()) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(
            "Requested byte_size: " + std::to_string(byte_size) +
            " is greater than total cache size: " +
            std::to_string(managed_buffer_.get_size()))
            .c_str());
  }
  // Attempt to allocate buffer from current available space
  void* lbuffer = nullptr;
  while (!lbuffer) {
    lbuffer = managed_buffer_.allocate(byte_size, std::nothrow_t{});
    // There wasn't enough available space, so evict and try again
    if (!lbuffer) {
      // Fail if we run out of things to evict
      RETURN_IF_ERROR(Evict());
    }
  }
  // Return allocated buffer
  *buffer = lbuffer;
  return nullptr;  // success
}


bool RedisCache::Exists(const std::string& key) {
  return this->_client->exists(key);
}

std::pair<TRITONSERVER_Error*, CacheEntry>
RedisCache::Lookup(const std::string& key)
{

  // Search cache for request hash key
  bool found = this->Exists(key);

  if (!found) {
    auto err = TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_NOT_FOUND,
        std::string("key [" + key + "] does not exist").c_str());
      return {err, {}};
  }

  return cache_get(key);
}

TRITONSERVER_Error*
RedisCache::Insert(const std::string& key, CacheEntry& entry) {

  // Exit early if key already exists in cache
  // Search cache for request hash key
  bool found = this->Exists(key);

  if (!found) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_ALREADY_EXISTS,
        std::string("Failed to insert key [" + key + "] into map").c_str());
  }

  // Construct cache entry from response
  auto cache_entry = CacheEntry();
  RETURN_IF_ERROR(cache_set(key, cache_entry));

  return nullptr; //success
}


TRITONSERVER_Error*
RedisCache::cache_set(std::string key, CacheEntry &cache_entry) {

    // set number of entries in the top level
    const std::string& entries_k = "entries";
    const std::string& entries_v = std::to_string(cache_entry.num_entries);
    cache_entry.items_.insert({entries_k, entries_v});

    // set response in a redis hash field
    try {
      _client->hmset(
        key,
        cache_entry.items_.begin(),
        cache_entry.items_.end()
      );
    }
    catch (sw::redis::TimeoutError &e) {
      std::string err = "Timeout inserting key" + key + " into cache.";
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());

  }
    catch (sw::redis::IoError &e) {
      std::string err = "Failed to insert key" + key + " into cache.";
      return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str());
      }
    catch (...) {
      std::string err = "Failed to insert key" + key + " into cache.";
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, err.c_str());
    }

  return nullptr; //success
}


std::pair<TRITONSERVER_Error*, CacheEntry>
RedisCache::cache_get(std::string key) {

    CacheEntry cache_entry = CacheEntry();

    try {
      _client->hgetall(
        key,
        std::inserter(cache_entry.items_, cache_entry.items_.begin())
      );

    }
    catch (sw::redis::TimeoutError &e) {
      std::string err = "Timeout inserting key" + key + " into cache.";
      return {TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str()), {}};
    }
    catch (sw::redis::IoError &e) {
      std::string err = "Failed to insert key" + key + " into cache.";
      return {TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string(err + e.what()).c_str()), {}};
    }
    catch (...) {
      std::string err = "Failed to insert key" + key + " into cache.";
      return {TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL, err.c_str()), {}};

    }

    // emptiness check
    if (cache_entry.items_.empty()) {
      return {TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        std::string("failed to retrieve key " + key + " from cache").c_str()), {}};
    }

    // set number of entries at the top level
    const char* entries = cache_entry.items_.at("entries").c_str();
    cache_entry.num_entries = std::atoi(entries);

    return {nullptr, cache_entry}; //success
  }

}}} // namespace triton::cache::redis
