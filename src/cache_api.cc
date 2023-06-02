#include "redis_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"

namespace triton::cache::redis {

enum fieldType{
  buffer,
  bufferSize,
  memoryType,
  memoryTypeId,
  cudaIpcHandle
};

std::string getFieldName(size_t bufferNumber, fieldType fieldType){
  switch (fieldType) {
    case buffer:
      return std::to_string(bufferNumber) + ":b";
    case bufferSize:
      return std::to_string(bufferNumber) + ":s";
    case memoryType:
      return std::to_string(bufferNumber) + ":t";
    case memoryTypeId:
      return std::to_string(bufferNumber) + ":i";
    case cudaIpcHandle:
      return std::to_string(bufferNumber) + ":c";
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
  std::cout << "In Cache Initialize" << std::endl;
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
  auto [err, redisEntry] = redis_cache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  size_t numBuffers = redisEntry.numBuffers;
  std::unordered_map<std::string, std::string> values = redisEntry.items;
  std::vector<std::shared_ptr<char[]>> buffers;

  if(numBuffers == 0){
    return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_NOT_FOUND, "did not find entry");
  }
  for (size_t i = 0; i < numBuffers; i++) {
    auto bufferFieldName = getFieldName(i, fieldType::buffer);
    auto bufferSizeFieldName = getFieldName(i, fieldType::bufferSize);
    auto memoryTypeFieldName = getFieldName(i, fieldType::memoryType);
    auto memoryTypeIdFieldName = getFieldName(i, fieldType::memoryTypeId);

    if(!(redisEntry.items.contains(bufferFieldName) &&
          redisEntry.items.contains(bufferSizeFieldName) &&
          redisEntry.items.contains(memoryTypeFieldName))){
      auto msg = "Error: encountered incomplete cache result.";
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg);
    }
    TRITONSERVER_BufferAttributes* attrs = nullptr;

    size_t byteSize = std::stoul(redisEntry.items.at(bufferSizeFieldName));
    int memoryTypeIntegralValue = std::stoi(redisEntry.items.at(memoryTypeFieldName));
    int64_t memoryTypeIdValue = std::stoi(redisEntry.items.at(memoryTypeIdFieldName));

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryTypeId(attrs, memoryTypeIdValue));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetByteSize(attrs, byteSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryType(attrs, (TRITONSERVER_memorytype_enum)memoryTypeIntegralValue));

    auto buffer = redisEntry.items.at(bufferFieldName);
    std::shared_ptr<char[]> bufferPtr(new char[byteSize]);
    buffers.push_back(bufferPtr);
    memcpy(bufferPtr.get(), buffer.c_str(), byteSize);

    RETURN_IF_ERROR(TRITONCACHE_CacheEntryAddBuffer(entry, bufferPtr.get(), attrs));

    TRITONSERVER_BufferAttributesDelete(attrs);
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
  std::vector<char*> buffersToFree;
  std::vector<std::shared_ptr<char[]>> managedBuffers;
  for(size_t i = 0; i < numBuffers; i++){
    TRITONSERVER_BufferAttributes* attrs = nullptr;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    std::shared_ptr<TRITONSERVER_BufferAttributes> managed_attrs(
        attrs, TRITONSERVER_BufferAttributesDelete);
    void* base = nullptr;
    size_t byteSize = 0;
    int64_t memoryTypeId;
    TRITONSERVER_MemoryType  memoryType;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetBuffer(entry, i, &base, attrs));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesByteSize(attrs, &byteSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesMemoryType(attrs, &memoryType));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesMemoryTypeId(attrs, &memoryTypeId));

    if(!byteSize){
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, "Buffer size was zero");
    }
    // DLIS-2673: Add better memory_type support - SL - keeping this in place, presumably we're going to have to pull out the other bits that are important some day.
    if (memoryType != TRITONSERVER_MEMORY_CPU &&
        memoryType != TRITONSERVER_MEMORY_CPU_PINNED) {
      return TRITONSERVER_ErrorNew(
          TRITONSERVER_ERROR_INVALID_ARG,
          "Only input buffers in CPU memory are allowed in cache currently");
    }

    std::shared_ptr<char[]> managedBuffer(new char[byteSize]);

    TRITONCACHE_CacheEntrySetBuffer(entry, i, (void*)managedBuffer.get(), nullptr);

    managedBuffers.push_back(managedBuffer);
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::bufferSize), std::to_string(byteSize)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::memoryType), std::to_string(memoryType)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::memoryTypeId), std::to_string(memoryTypeId)));
  }

  TRITONCACHE_Copy(allocator, entry);
  for (size_t i = 0; i < numBuffers; i++)
  {
    auto bytesToCopy = std::stoi(redis_entry.items.at(getFieldName(i, fieldType::bufferSize)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::buffer), std::string(managedBuffers.at(i).get(), bytesToCopy)));
  }

  for(auto buffer : buffersToFree){
    delete[] buffer;
  }

  RETURN_IF_ERROR(redis_cache->Insert(key, redis_entry));
  return nullptr;  // success
}
}  // extern "C"
}  // namespace triton::cache::redis
