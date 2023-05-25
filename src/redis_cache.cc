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
#include "triton/core/tritoncache.h"


namespace triton::cache::redis {


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

bool RedisCache::Exists(const std::string& key) {
  return this->_client->exists(key);
}

std::pair<TRITONSERVER_Error*, CacheEntry>
RedisCache::Lookup(const std::string& key)
{
  CacheEntry entry;

  try{
    this->_client->hgetall(key, std::inserter(entry.items,entry.items.begin()));
    entry.numBuffers = entry.items.size() / 3;
    return {nullptr, entry};
  }
  catch (sw::redis::TimeoutError &e){
    std::string msg = "Timeout retrieving key: " + key + " from cache " + e.what() ;
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
  catch( sw::redis::IoError &e){
    std::string msg = "Failed to retrieve key: " + key + " from cache " + e.what();
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
  catch (...){
    std::string msg = "Failed to retrieve key: " + key + " from cache";
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
}

TRITONSERVER_Error*
RedisCache::Insert(const std::string& key, CacheEntry& entry) {
  try {
      _client->hmset(
          key,
          entry.items.begin(),
          entry.items.end()
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
} // namespace triton::cache::redis
