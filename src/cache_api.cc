#include "redis_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"
#include <format>

namespace triton::cache::redis {

enum fieldType{
  buffer,
  bufferSize,
  memoryType
};

std::string getFieldName(size_t bufferNumber, fieldType fieldType){
  switch (fieldType) {
    case buffer:
      return std::to_string(bufferNumber) + ":b";
    case bufferSize:
      return std::to_string(bufferNumber) + ":s";
    case memoryType:
      return std::to_string(bufferNumber) + ":t";
  }
}

std::string suffix_key(std::string key, int suffix) {
  std::string s_key = key + ":" + std::to_string(suffix);
  return s_key;
}

extern "C" {

// Helper
TRITONSERVER_Error*
CheckArgs(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry,
    TRITONCACHE_Allocator* allocator)
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
  } else if (allocator == nullptr) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG, "allocator was nullptr");
  }

  return nullptr;  // success
}

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

  std::unique_ptr<RedisCache> rcache;
  RETURN_IF_ERROR(RedisCache::Create(cache_config, &rcache));
  *cache = reinterpret_cast<TRITONCACHE_Cache*>(rcache.release());
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
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry, TRITONCACHE_Allocator* allocator)
{
  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));

  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  auto [err, redis_entry] = redis_cache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  size_t numBuffers = redis_entry.numBuffers;
  std::unordered_map<std::string, std::string> values = redis_entry.items;
  for (size_t i = 1; i <= numBuffers; i++) {
    auto bufferFieldName = getFieldName(i, fieldType::buffer);
    auto bufferSizeFieldName = getFieldName(i, fieldType::bufferSize);
    auto memoryTypeFieldName = getFieldName(i, fieldType::memoryType);

    if(!(redis_entry.items.contains(bufferFieldName) && redis_entry.items.contains(bufferSizeFieldName) && redis_entry.items.contains(memoryTypeFieldName))){
      auto msg = "Error: encountered incomplete cache result.";
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg);
    }
    TRITONSERVER_BufferAttributes* attrs = nullptr;

    size_t bufferSize = std::stoul(redis_entry.items.at(bufferSizeFieldName));
    int memoryTypeIntegralValue = std::stoi(redis_entry.items.at(memoryTypeFieldName));

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetByteSize(attrs, bufferSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryType(attrs, (TRITONSERVER_memorytype_enum)memoryTypeIntegralValue));

    RETURN_IF_ERROR(TRITONCACHE_CacheEntryAddBuffer(entry, (void*)redis_entry.items.at(bufferFieldName).c_str(), attrs));
  }

  RETURN_IF_ERROR(TRITONCACHE_Copy(allocator, entry));
  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheInsert(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry, TRITONCACHE_Allocator* allocator)
{

  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));
  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  CacheEntry redis_entry;
  size_t numBuffers = 0;
  RETURN_IF_ERROR(TRITONCACHE_CacheEntryBufferCount(entry, &numBuffers));

  for(size_t i = 0; i < numBuffers; i++){
    void* base = nullptr;
    TRITONSERVER_BufferAttributes* attrs = nullptr;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    std::shared_ptr<TRITONSERVER_BufferAttributes> managed_attrs(attrs, TRITONSERVER_BufferAttributesDelete);

    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetBuffer(entry, i, &base, attrs));

    size_t byteSize = 0;

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesByteSize(attrs, &byteSize));

    if(!byteSize){
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, "Buffer size was zero");
    }

    TRITONSERVER_MemoryType  memoryType;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesMemoryType(attrs, &memoryType));
    // DLIS-2673: Add better memory_type support - SL - keeping this in place, presumably we're going to have to pull out the other bits that are important some day.
    if (memoryType != TRITONSERVER_MEMORY_CPU &&
        memoryType != TRITONSERVER_MEMORY_CPU_PINNED) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INVALID_ARG,
          "Only input buffers in CPU memory are allowed in cache currently");
    }

    auto bufferNumStr = std::to_string(i);

    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::buffer), std::string((char*)base)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::bufferSize), std::to_string(byteSize)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::memoryType), std::to_string(memoryType)));
  }

  RETURN_IF_ERROR(redis_cache->Insert(key, redis_entry));
  return nullptr;  // success
}

}  // extern "C"

}  // namespace triton::cache::redis
