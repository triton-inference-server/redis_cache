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

#include "rapidjson/document.h"
#include "redis_cache.h"
#include "triton/common/logging.h"
#include "triton/core/tritoncache.h"

namespace triton::cache::redis {

std::unique_ptr<sw::redis::Redis>
init_client(
    const sw::redis::ConnectionOptions& connectionOptions,
    sw::redis::ConnectionPoolOptions poolOptions)
{
  std::unique_ptr<sw::redis::Redis> redis =
      std::make_unique<sw::redis::Redis>(connectionOptions, poolOptions);
  auto res = redis->ping("pong");
  LOG_VERBOSE("Successfully connected to Redis");
  return redis;
}


TRITONSERVER_Error*
RedisCache::Create(
    const std::string& cache_config, std::unique_ptr<RedisCache>* cache)
{
  rapidjson::Document document;

  document.Parse(cache_config.c_str());
  if (!document.HasMember("host") || !document.HasMember("port")) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG,
        "Failed to initialize RedisCache, didn't specify address. Must at a "
        "minimum specify 'host' and 'port' in the configuration - e.g. "
        "tritonserver --cache-config redis,host=redis --cache-config "
        "redis,port=6379 --model-repository=/models ...");
  }

  sw::redis::ConnectionOptions options;
  sw::redis::ConnectionPoolOptions poolOptions;

  if (document.HasMember("host")) {
    options.host = document["host"].GetString();
  }
  if (document.HasMember("port")) {
    options.port = std::atoi(document["port"].GetString());
  }
  if (document.HasMember("user")) {
    options.user = document["user"].GetString();
  }
  if (document.HasMember("password")) {
    options.password = document["password"].GetString();
  }
  if (document.HasMember("db")) {
    options.db = std::atoi(document["db"].GetString());
  }
  if (document.HasMember("connect_timeout")) {
    auto ms = std::atoi(document["connect_timeout"].GetString());
    options.connect_timeout = std::chrono::milliseconds(ms);
  }
  if (document.HasMember("socket_timeout")) {
    auto ms = std::atoi(document["socket_timeout"].GetString());
    options.socket_timeout = std::chrono::milliseconds(ms);
  }
  if (document.HasMember("pool_size")) {
    poolOptions.size = std::atoi(document["pool_size"].GetString());
  }
  if (document.HasMember("wait_timeout")) {
    auto ms = std::atoi(document["wait_timeout"].GetString());
    poolOptions.wait_timeout = std::chrono::milliseconds(ms);
  } else {
    poolOptions.wait_timeout = std::chrono::milliseconds(100);
  }

  try {
    cache->reset(new RedisCache(options, poolOptions));
  }
  catch (const std::exception& ex) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL,
        ("Failed to initialize Response Cache: " + std::string(ex.what()))
            .c_str());
  }
  return nullptr;  // success
}

RedisCache::RedisCache(
    const sw::redis::ConnectionOptions& connectionOptions,
    const sw::redis::ConnectionPoolOptions& poolOptions)
{
  try {
    this->_client = init_client(connectionOptions, poolOptions);
  }
  catch (const std::exception& ex) {
    throw std::runtime_error(
        ("Failed to initialize Response Cache: " + std::string(ex.what()))
            .c_str());
  }
}

RedisCache::~RedisCache()
{
  this->_client.reset();
}

std::pair<TRITONSERVER_Error*, CacheEntry>
RedisCache::Lookup(const std::string& key)
{
  CacheEntry entry;

  try {
    this->_client->hgetall(
        key, std::inserter(entry.items, entry.items.begin()));

    // determine the number of buffers by dividing the size by the number of
    // fields per buffer
    entry.numBuffers = entry.items.size() / FIELDS_PER_BUFFER;
    return {nullptr, entry};
  }
  catch (sw::redis::TimeoutError& e) {
    std::string msg =
        "Timeout retrieving key: " + key + " from cache " + e.what();
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
  catch (sw::redis::IoError& e) {
    std::string msg =
        "Failed to retrieve key: " + key + " from cache " + e.what();
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
  catch (...) {
    std::string msg = "Failed to retrieve key: " + key + " from cache";
    auto err = TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg.c_str());
    return {err, {}};
  }
}

TRITONSERVER_Error*
RedisCache::Insert(const std::string& key, CacheEntry& entry)
{
  try {
    _client->hmset(key, entry.items.begin(), entry.items.end());
  }
  catch (sw::redis::TimeoutError& e) {
    std::string err = "Timeout inserting key" + key + " into cache.";
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL, std::string(err + e.what()).c_str());
  }
  catch (sw::redis::IoError& e) {
    std::string err = "Failed to insert key" + key + " into cache.";
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INTERNAL, std::string(err + e.what()).c_str());
  }
  catch (...) {
    std::string err = "Failed to insert key" + key + " into cache.";
    return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, err.c_str());
  }

  return nullptr;  // success
}
}  // namespace triton::cache::redis
