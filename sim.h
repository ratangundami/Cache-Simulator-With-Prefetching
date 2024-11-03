#ifndef SIM_CACHE_H
#define SIM_CACHE_H

typedef struct {
   uint32_t BLOCKSIZE;
   uint32_t L1_SIZE;
   uint32_t L1_ASSOC;
   uint32_t L2_SIZE;
   uint32_t L2_ASSOC;
   uint32_t PREF_N;
   uint32_t PREF_M;
} cache_params_t;

typedef struct {
    bool valid;
    uint32_t tag;
    bool dirty_bit;
    int lru;
} cache_block_t;

typedef struct {
    bool valid;
    int buffer_name;
    uint32_t* buffer;
    int lru;
} stream_buffer_t;

#endif
