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

#pragma once

#include <sw/redis++/connection.h>
#include <sw/redis++/redis++.h>

#include <iostream>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>

#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"

namespace triton::cache::redis {

struct CacheEntry {
  size_t numBuffers = 0;
  std::unordered_map<std::string, std::string> items;
};

// This is the number of fields that are created to each buffer to marshal
// the buffer back to Triton
constexpr uint32_t FIELDS_PER_BUFFER = 4;

constexpr const char* PASSWORD_ENV_VAR_NAME = "TRITONCACHE_REDIS_PASSWORD";
constexpr const char* USERNAME_ENV_VAR_NAME = "TRITONCACHE_REDIS_USERNAME";


#define RETURN_IF_ERROR(X)        \
  do {                            \
    TRITONSERVER_Error* err__(X); \
    if (err__ != nullptr) {       \
      return err__;               \
    }                             \
  } while (false)


class RedisCache {
 public:
  ~RedisCache();

  // Create the request/response cache object
  static TRITONSERVER_Error* Create(
      const std::string& cache_config, std::unique_ptr<RedisCache>* cache);

  // Lookup key in cache and return the data associated with it
  // Return TRITONSERVER_Error* object indicating success or failure.
  std::pair<TRITONSERVER_Error*, CacheEntry> Lookup(const std::string& key);

  // Insert entry into cache, evict entries to make space if necessary
  // Return TRITONSERVER_Error* object indicating success or failure.
  TRITONSERVER_Error* Insert(const std::string& key, CacheEntry& entry);

 private:
  explicit RedisCache(
      const sw::redis::ConnectionOptions& connectionOptions,
      const sw::redis::ConnectionPoolOptions& poolOptions);

  std::unique_ptr<sw::redis::Redis> _client;
};

}  // namespace triton::cache::redis
