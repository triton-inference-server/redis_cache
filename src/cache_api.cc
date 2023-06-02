#include "redis_cache.h"
#include "triton/core/tritoncache.h"
#include "triton/core/tritonserver.h"
#include <cstdio>

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

  std::cout << "In Cache Lookup" << std::endl;
  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));

  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  auto [err, redisEntry] = redis_cache->Lookup(key);
  if (err != nullptr) {
    return err;
  }

  size_t numBuffers = redisEntry.numBuffers;
  std::unordered_map<std::string, std::string> values = redisEntry.items;

  if(numBuffers == 0){
    return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_NOT_FOUND, "did not find entry");
  }
  std::cout << "Number of Buffers: " << numBuffers << std::endl;
  for (size_t i = 0; i < numBuffers; i++) {
    auto bufferFieldName = getFieldName(i, fieldType::buffer);
    auto bufferSizeFieldName = getFieldName(i, fieldType::bufferSize);
    auto memoryTypeFieldName = getFieldName(i, fieldType::memoryType);
    auto memoryTypeIdFieldName = getFieldName(i, fieldType::memoryTypeId);

    if(!(redisEntry.items.contains(bufferFieldName) &&
          redisEntry.items.contains(bufferSizeFieldName) &&
          redisEntry.items.contains(memoryTypeFieldName))){
      auto msg = "Error: encountered incomplete cache result.";
      std::cout <<"Entry missing item" << std::endl;
      return TRITONSERVER_ErrorNew(TRITONSERVER_ERROR_INTERNAL, msg);
    }
    TRITONSERVER_BufferAttributes* attrs = nullptr;

    size_t byteSize = std::stoul(redisEntry.items.at(bufferSizeFieldName));
    int memoryTypeIntegralValue = std::stoi(redisEntry.items.at(memoryTypeFieldName));
    int64_t memoryTypeIdValue = std::stoi(redisEntry.items.at(memoryTypeIdFieldName));

    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));

    std::cout << "byteSize Address " << &byteSize << std::endl;
    std::cout << "byteSize " << byteSize << std::endl;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryTypeId(attrs, memoryTypeIdValue));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetByteSize(attrs, byteSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetMemoryType(attrs, (TRITONSERVER_memorytype_enum)memoryTypeIntegralValue));

    size_t bytesInBufferAttributes;
    TRITONSERVER_BufferAttributesByteSize(attrs, &bytesInBufferAttributes);

    std::cout << "Byte size according to attributes: " << bytesInBufferAttributes << std::endl;

    auto buffer = redisEntry.items.at(bufferFieldName);

    std::cout << "Byte Size: " << byteSize << std::endl;
    std::cout << "Memory Type: " << memoryTypeIntegralValue << std::endl;
    auto *bufferPtr = new char[byteSize];
    memcpy(bufferPtr, buffer.c_str(), byteSize);
    std::cout <<"Copied buffer" << std::endl;


    auto res = TRITONCACHE_CacheEntryAddBuffer(entry, bufferPtr, attrs);

    if(res != nullptr){
      std::cout <<"Buffer add operation disposition: " << res << std::endl << std::flush;
    }

    RETURN_IF_ERROR(res);
  }

  std::cout << "Copying " << std::endl;
  auto copyRes = TRITONCACHE_Copy(allocator, entry);

  if(copyRes != nullptr){
    std::cout <<"Copy result: " << TRITONSERVER_ErrorMessage(copyRes) << std::endl;
  }

  RETURN_IF_ERROR(copyRes);

  std::cout << "Finished lookup" << std::endl;
  std::cout << std::flush;
  return nullptr;  // success
}

TRITONSERVER_Error*
TRITONCACHE_CacheInsert(
    TRITONCACHE_Cache* cache, const char* key, TRITONCACHE_CacheEntry* entry, TRITONCACHE_Allocator* allocator)
{
  std::cout << "In Cache Insert" << std::endl;
  RETURN_IF_ERROR(CheckArgs(cache, key, entry, allocator));
  const auto redis_cache = reinterpret_cast<RedisCache*>(cache);
  CacheEntry redis_entry;
  size_t numBuffers = 0;
  RETURN_IF_ERROR(TRITONCACHE_CacheEntryBufferCount(entry, &numBuffers));
  std::vector<char*> buffersToFree;

//  RETURN_IF_ERROR(TRITONCACHE_Copy(allocator, entry));


  for(size_t i = 0; i < numBuffers; i++){
    TRITONSERVER_BufferAttributes* attrs = nullptr;
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesNew(&attrs));
    std::shared_ptr<TRITONSERVER_BufferAttributes> managed_attrs(
        attrs, TRITONSERVER_BufferAttributesDelete);
    void* base = nullptr;
    size_t byteSize = 0;
    int64_t memoryTypeId;
    TRITONSERVER_MemoryType  memoryType;
    void* ipcHandle;
    RETURN_IF_ERROR(TRITONCACHE_CacheEntryGetBuffer(entry, i, &base, attrs));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesByteSize(attrs, &byteSize));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesSetCudaIpcHandle(attrs, &ipcHandle));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesMemoryType(attrs, &memoryType));
    RETURN_IF_ERROR(TRITONSERVER_BufferAttributesMemoryTypeId(attrs, &memoryTypeId));


    if(true){
      std::cout << "Cache Entry Insert Attributes for Buffer:" << std::endl;
      std::cout << "Cuda IPC Handle: " <<  cudaIpcHandle << std::endl;
      std::cout << "Memory Type: " << memoryType << std::endl;
      std::cout << "Memory Type ID: " << memoryTypeId << std::endl;
      std::cout << "Byte Size: " << byteSize << std::endl;
    }

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

    auto bufferNumStr = std::to_string(i);

    auto *bufferStr = new char[byteSize];

    memcpy(bufferStr, &base, byteSize);

    auto str = std::string(bufferStr, byteSize);

    std::cout << "Byte size at Insert " << byteSize << std::endl;

    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::buffer), str));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::bufferSize), std::to_string(byteSize)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::memoryType), std::to_string(memoryType)));
//    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::cudaIpcHandle), std::to_string(&cudaIpcHandle)));
    redis_entry.items.insert(std::make_pair(getFieldName(i, fieldType::memoryTypeId), std::to_string(memoryTypeId)));

//    delete[] bufferStr;
  }

  RETURN_IF_ERROR(redis_cache->Insert(key, redis_entry));
  return nullptr;  // success
}

}  // extern "C"

}  // namespace triton::cache::redis
