#include "redis_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"

namespace triton::cache::redis {

enum fieldType { buffer, bufferSize, memoryType, memoryTypeId };

std::string
getFieldName(size_t bufferNumber, fieldType fieldType)
{
  switch (fieldType) {
    case buffer:
      return std::to_string(bufferNumber) + ":b";
    case bufferSize:
      return std::to_string(bufferNumber) + ":s";
    case memoryType:
      return std::to_string(bufferNumber) + ":t";
    case memoryTypeId:
      return std::to_string(bufferNumber) + ":i";
  }
  return "";
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
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry,
    TRITONCACHE_Allocator* allocator)
{
  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));

  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  auto [err, redisEntry] = redis_cache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  size_t numBuffers = redisEntry.numBuffers;
  std::unordered_map<std::string, std::string> values = redisEntry.items;

  if (numBuffers == 0) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_NOT_FOUND, "did not find entry");
  }
  for (size_t i = 0; i < numBuffers; i++) {
    auto bufferFieldName = getFieldName(i, fieldType::buffer);
    auto bufferSizeFieldName = getFieldName(i, fieldType::bufferSize);
    auto memoryTypeFieldName = getFieldName(i, fieldType::memoryType);
    auto memoryTypeIdFieldName = getFieldName(i, fieldType::memoryTypeId);

    if (redisEntry.items.size() % FIELDS_PER_BUFFER != 0 ||
        !(redisEntry.items.contains(bufferFieldName) &&
          redisEntry.items.contains(bufferSizeFieldName) &&
          redisEntry.items.contains(memoryTypeFieldName) &&
          redisEntry.items.contains(memoryTypeIdFieldName))) {
      auto msg = "Error: encountered incomplete cache result.";
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg);
    }
    TRITONSERVER_BufferAttributes* attrs = nullptr;

    size_t byteSize = std::stoul(redisEntry.items.at(bufferSizeFieldName));
    int memoryTypeIntegralValue =
        std::stoi(redisEntry.items.at(memoryTypeFieldName));
    int64_t memoryTypeIdValue =
        std::stoi(redisEntry.items.at(memoryTypeIdFieldName));

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    RETURN_IF_ERROR(
        TRITONSERVER_BufferAttributesSetMemoryTypeId(attrs, memoryTypeIdValue));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetByteSize(attrs, byteSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryType(
        attrs, (TRITONSERVER_memorytype_enum)memoryTypeIntegralValue));

    const void* buffer = redisEntry.items.at(bufferFieldName).c_str();

    RETURN_IF_ERROR(TRITONCACHE_CacheEntryAddBuffer(
        entry, const_cast<void*>(buffer), attrs));

    TRITONSERVER_BufferAttributesDelete(attrs);
  }

  // Callback to copy directly from RedisCache buffers into Triton buffers
  RETURN_IF_ERROR(TRITONCACHE_Copy(allocator, entry));

  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheInsert(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry,
    TRITONCACHE_Allocator* allocator)
{
  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));
  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  CacheEntry redis_entry;
  size_t numBuffers = 0;
  RETURN_IF_ERROR(TRITONCACHE_CacheEntryBufferCount(entry, &numBuffers));
  std::vector<std::shared_ptr<char[]>> managedBuffers;
  for (size_t i = 0; i < numBuffers; i++) {
    TRITONSERVER_BufferAttributes* attrs = nullptr;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    std::shared_ptr<TRITONSERVER_BufferAttributes> managed_attrs(
        attrs, TRITONSERVER_BufferAttributesDelete);
    void* base = nullptr;
    size_t byteSize = 0;
    int64_t memoryTypeId;
    TRITONSERVER_MemoryType memoryType;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetBuffer(entry, i, &base, attrs));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesByteSize(attrs, &byteSize));
    RETURN_IF_ERROR(
        TRITONSERVER_BufferAttributesMemoryType(attrs, &memoryType));
    RETURN_IF_ERROR(
        TRITONSERVER_BufferAttributesMemoryTypeId(attrs, &memoryTypeId));

    if (!byteSize) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INTERNAL, "Buffer size was zero");
    }
    // DLIS-2673: Add better memory_type support - SL - keeping this in place,
    // presumably we're going to have to pull out the other bits that are
    // important some day.
    if (memoryType != TRITONSERVER_MEMORY_CPU &&
        memoryType != TRITONSERVER_MEMORY_CPU_PINNED) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INVALID_ARG,
          "Only input buffers in CPU memory are allowed in cache currently");
    }

    std::shared_ptr<char[]> managedBuffer(new char[byteSize]);

    // Overwrite entry buffer with cache-allocated buffer.
    // No need to set new buffer attrs for now, will reuse the one we got above.
    TRITONCACHE_CacheEntrySetBuffer(
        entry, i, static_cast<void*>(managedBuffer.get()), nullptr /* attrs */);

    managedBuffers.push_back(managedBuffer);
    redis_entry.items.insert(std::make_pair(
        getFieldName(i, fieldType::bufferSize), std::to_string(byteSize)));
    redis_entry.items.insert(std::make_pair(
        getFieldName(i, fieldType::memoryType), std::to_string(memoryType)));
    redis_entry.items.insert(std::make_pair(
        getFieldName(i, fieldType::memoryTypeId),
        std::to_string(memoryTypeId)));
  }

  // Callback to copy directly from Triton buffers to RedisCache managedBuffers
  TRITONCACHE_Copy(allocator, entry);
  for (size_t i = 0; i < numBuffers; i++) {
    auto bytesToCopy =
        std::stoi(redis_entry.items.at(getFieldName(i, fieldType::bufferSize)));
    redis_entry.items.insert(std::make_pair(
        getFieldName(i, fieldType::buffer),
        std::string(managedBuffers.at(i).get(), bytesToCopy)));
  }

  // sanity check to make sure we are inserting items into the cache that are
  // comprised of the right number of fields to allow us to marshal
  // the buffer back from Redis later on.
  if (redis_entry.items.size() % FIELDS_PER_BUFFER != 0) {
    return TRITONSERVER_ErrorNew(
        TRITONSERVER_ERROR_INVALID_ARG,
        "Attempted to add incomplete entry to cache");
  }

  RETURN_IF_ERROR(redis_cache->Insert(key, redis_entry));
  return nullptr;  // success
}
}  // extern "C"
}  // namespace triton::cache::redis
