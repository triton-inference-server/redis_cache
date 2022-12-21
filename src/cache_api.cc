#include "local_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"

namespace triton { namespace cache { namespace local {

extern "C" {

TRITONSERVER_Error*
TRITONCACHE_CacheNew(TRITONCACHE_Cache** cache, const char* cache_config)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  }
  if (cache_config == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  }

  std::unique_ptr<LocalCache> lcache;
  RETURN_IF_ERROR(LocalCache::Create(cache_config, &lcache));
  *cache = reinterpret_cast<TRITONCACHE_Cache*>(lcache.release());
  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheDelete(TRITONCACHE_Cache* cache)
{
  if (cache == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "cache was nullptr");
  }

  delete reinterpret_cast<LocalCache*>(cache);
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

  const auto lcache = reinterpret_cast<LocalCache*>(cache);
  auto [err, lentry] = lcache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  for (const auto& item : lentry.items_) {
    TRITONCACHE_CacheEntryItem* triton_item = nullptr;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemNew(&triton_item));
    for (const auto& [buffer, byte_size] : item.buffers_) {
      if (!buffer || !byte_size) {
        return TRITONSERVER_ErrorNew(
            TRITONSERVER_ERROR_INTERNAL, "buffer was null or size was zero");
      }

      // DLIS-2673: Add better memory_type support
      TRITONSERVER_MemoryType memory_type = TRITONSERVER_MEMORY_CPU;
      int64_t memory_type_id = 0;
      RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemAddBuffer(
          triton_item, buffer, byte_size, memory_type, memory_type_id));
    }

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

  const auto lcache = reinterpret_cast<LocalCache*>(cache);
  if (lcache->Exists(key)) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_ALREADY_EXISTS,
        (std::string("key '") + key + std::string("' already exists")).c_str());
  }

  size_t num_items = 0;
  RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemCount(entry, &num_items));

  // Form cache representation of CacheEntry from Triton
  CacheEntry lentry;
  for (size_t item_index = 0; item_index < num_items; item_index++) {
    TRITONCACHE_CacheEntryItem* item = nullptr;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetItem(entry, item_index, &item));

    size_t num_buffers = 0;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryItemBufferCount(item, &num_buffers));

    // Form cache representation of CacheEntryItem from Triton
    CacheEntryItem litem;
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

      // Cache will replace this base pointer with a new cache-allocated base
      // pointer internally on Insert()
      litem.buffers_.emplace_back(std::make_pair(base, byte_size));
    }
    lentry.items_.emplace_back(litem);
  }

  RETURN_IF_ERROR(lcache->Insert(key, lentry));
  return nullptr;  // success
}

}  // extern "C"

}}}  // namespace triton::cache::local
