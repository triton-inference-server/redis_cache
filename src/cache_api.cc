#include "redis_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"

namespace triton { namespace cache { namespace redis {

std::string suffix_key(std::string key, int suffix) {
  std::string s_key = key + "_" + std::to_string(suffix);
  return s_key;
}

extern "C" {

TRITONSERVER_Error*
TRITONCACHE_CacheInitialize(TRITONCACHE_Cache** cache, const char* cache_config)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  }
  if (cache_config == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache config was nullptr");
  }

  std::unique_ptr<RedisCache> lcache;
  RETURN_IF_ERROR(RedisCache::Create(cache_config, &lcache));
  *cache = reinterpret_cast<TRITONCACHE_Cache*>(lcache.release());
  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheFinalize(TRITONCACHE_Cache* cache)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  }

  delete reinterpret_cast<RedisCache*>(cache);
  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheLookup(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  } else if (entry == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache entry was nullptr");
  }

  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  auto [err, redis_entry] = redis_cache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  int entries = redis_entry.num_entries;
  std::unordered_map<std::string, std::string> values = redis_entry.items_;
  for (int i = 1; i <= entries; i++) {
    TRITONCACHE_CacheEntryItem* triton_item = nullptr;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemNew(&triton_item));

    std::string byte_size_str = values.at(suffix_key("s", i));
    if (byte_size_str.empty()) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INTERNAL, "byte size was empty");
    }
    size_t byte_size = std::stoul(byte_size_str);


    std::string buffer_str = values.at(suffix_key("b", i));
    if (buffer_str.empty()) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INTERNAL, "buffer was empty");
    }
    const void* buffer = buffer_str.c_str();

    // DLIS-2673: Add better memory_type support
    TRITONSERVER_MemoryType memory_type = TRITONSERVER_MEMORY_CPU;
    int64_t memory_type_id = 0;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemAddBuffer(
        triton_item, buffer, byte_size, memory_type, memory_type_id));

    // Pass ownership of triton_item to Triton to avoid copy. Triton will
    // be responsible for cleaning it up, so do not call CacheEntryItemDelete.
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryAddItem(entry, triton_item));
  }

  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheInsert(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  } else if (entry == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache entry was nullptr");
  } else if (key == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "key was nullptr");
  }

  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);

  // TODO debate whether we should allow overwrites
  if (redis_cache->Exists(key)) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_ALREADY_EXISTS,
        (std::string("key '") + key + std::string("' already exists")).c_str());
  }

  size_t num_items = 0;
  RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemCount(entry, &num_items));

  // Form cache representation of CacheEntry from Triton
  CacheEntry redis_entry;
  for (size_t item_index = 0; item_index < num_items; item_index++) {
    TRITONCACHE_CacheEntryItem* item = nullptr;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetItem(entry, item_index, &item));

    size_t num_buffers = 0;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemBufferCount(item, &num_buffers));

    // add all buffers and sizes to the entry
    for (size_t buffer_index = 0; buffer_index < num_buffers; buffer_index++) {
      void* base = nullptr;
      size_t byte_size = 0;
      TRITONSERVER_MemoryType memory_type = TRITONSERVER_MEMORY_CPU;
      int64_t memory_type_id = 0;
      RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemGetBuffer(
          item, buffer_index, &base, &byte_size, &memory_type,
          &memory_type_id));

      // DLIS-2673: Add better memory_type support
      if (memory_type != TRITONSERVER_MEMORY_CPU &&
          memory_type != TRITONSERVER_MEMORY_CPU_PINNED) {
        return TRITONSERVER_ErrorNew(
            TRITONSERVER_ERROR_INVALID_ARG,
            "Only input buffers in CPU memory are allowed in cache currently");
      }

      if (!byte_size) {
        return TRITONSERVER_ErrorNew(
            TRITONSERVER_ERROR_INTERNAL, "buffer size was zero");
      }

      redis_entry.items_.insert(
          {suffix_key("b", item_index + 1), std::string((char*)base)});
      redis_entry.items_.insert(
          {suffix_key("s", item_index + 1), std::to_string(byte_size)});
    }
  }

  RETURN_IF_ERROR(redis_cache->Insert(key, redis_entry));
  return nullptr;  // success
}

}  // extern "C"

}}}  // namespace triton::cache::redis
