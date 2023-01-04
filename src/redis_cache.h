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

#pragma once

#include <list>
#include <string>
#include <iostream>
#include <string_view>
#include <unordered_map>

#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"
#include <sw/redis++/redis++.h>



namespace triton { namespace cache { namespace redis {



// cache entry structure
// in redis this will look like
// key - > b_1 : buffer
//         s_1 : size
//         b_2 : buffer
//         s_2 : size
//         ...
//         b_n : buffer
//         s_n : size
//
// this provides the ability to store more attributes
// of the buffer in the future if need be.
struct CacheEntry {
  int num_entries = 1;
  std::unordered_map<std::string, std::string> items_;
};


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

  // Checks if key exists in cache
  // Return true if key exists in cache, false otherwise.
  bool Exists(const std::string& key);

   // Evict entries from cache based on policy.
  // Return TRITONSERVER_Error* object indicating success or failure.
  TRITONSERVER_Error* Evict();

  TRITONSERVER_Error* Flush() {
    // empty the entire database (mostly used in testing)
    _client->flushall();
    return nullptr;
   // TODO return an error if it fails
  }

 private:
  explicit RedisCache(std::string address, std::string username, std::string password);

  // get/set
  TRITONSERVER_Error* cache_set(std::string key, CacheEntry &cache_entry);
  std::pair<TRITONSERVER_Error*, CacheEntry> cache_get(std::string key);

  std::unique_ptr<sw::redis::Redis> _client;
};

}}}  // namespace triton::core