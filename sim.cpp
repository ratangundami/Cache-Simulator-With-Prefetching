#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <cmath>
#include <string.h>
#include <algorithm>
#include <iomanip>
#include "sim.h"


using namespace std;


// Declarations
class Cache;
class Memory;
class MemoryBlock;
int CacheController(char rw, uint32_t addr, MemoryBlock* current, bool recursive);

// Global Variables
uint32_t parse_count = 0;
bool DEBUG_PRINT = false;
uint32_t MEMORY_TRAFFIC = 0;

class MemoryBlock{
public:
   Cache* data;
   int name;
   MemoryBlock* next;
   MemoryBlock* prev;

   MemoryBlock(Cache* data, int name){
      this->data = data;
      this->name = name;
      this->next = nullptr;
      this->prev = nullptr;
   }
};


class Memory{
public:
   MemoryBlock* head;
   MemoryBlock* tail;

   Memory(){
      this->head = nullptr;
      this->tail = nullptr;
   }

   ~Memory() {
      MemoryBlock* current = head;
      while (current != nullptr) {
         MemoryBlock* nextBlock = current->next;
         delete current;
         current = nextBlock;
      }
   }

   void insertNewCache(Cache* data, int name)
   {
      MemoryBlock* newCache = new MemoryBlock(data, name);

      // If list is empty.
      if (head == nullptr) {
         head = newCache;
         tail = newCache;
         return;
      }
      // else make tail link as new Cache.
      else {
         tail->next = newCache;
         newCache->prev = tail;
         tail = newCache;
      }
   }

   void displayMemory(){
      printf("\n MEMORY HIERARCHY : \n");
      MemoryBlock* temp = head;
      while (temp != nullptr) {
         printf(" -> Level : %d ", temp->name); 
         temp = temp->next;
      }
      printf("\n");
   }
};


class Cache{
   public:
      int SIZE;
      int ASSOC;
      int BLOCKSIZE;
      uint32_t ADDRESS;

      bool SB=false;
      int SB_N_COUNT = 0;
      int SB_M_BLOCKS = 0;

      cache_block_t** cache_memory;
      stream_buffer_t* stream_buffer;

      uint32_t READS = 0;
      uint32_t READMISSES = 0;
      uint32_t WRITES = 0;
      uint32_t WRITEMISSES = 0;
      uint32_t WRITEBACKS = 0;
      uint32_t PREFETCHREADS = 0;
      uint32_t PREFETCHMISSES = 0;
      uint32_t PREFETCHES = 0;

      int sets;
      unsigned int address_length = 32;
      unsigned int tag_bits;
      unsigned int index_bits;
      unsigned int blockoffset_bits;
      unsigned int tag;
      unsigned int index;
      unsigned int blockoffset;


   Cache(int b, int s, int a, int sb_n, int sb_m){
      BLOCKSIZE = b;
      SIZE = s;
      ASSOC = a;
      SB_N_COUNT = sb_n;
      SB_M_BLOCKS = sb_m;

      if(ASSOC != 0){
         sets = (uint32_t) SIZE / (ASSOC * BLOCKSIZE);
      }
      else{
         sets = 0;
      }


      // Dynamic memory allocation

      // Cache memory allocation
      cache_memory = new cache_block_t*[sets];
      for(int i = 0; i < sets; i++){
         cache_memory[i] = new cache_block_t[ASSOC];
      }

      // Stream Buffer allocation
      if (SB_N_COUNT != 0){
         SB = true;
         stream_buffer = new stream_buffer_t[SB_N_COUNT]; // Create buffer arrays
         
         // Allocate memory for each buffer
         for (int i = 0; i < SB_N_COUNT; i++) {
            stream_buffer[i].valid = 0;
            stream_buffer[i].lru = 0;
            stream_buffer[i].buffer = new uint32_t[SB_M_BLOCKS];
         }
      }
    
      // Initialization of Cache Memory
      for(int i = 0; i < sets; i++){
         for(int j = 0; j < ASSOC; j++){
               cache_memory[i][j].tag = 0;
               cache_memory[i][j].dirty_bit = 0;
               cache_memory[i][j].valid = 0;
               if (j == 0)
                  cache_memory[i][j].lru = 0;
               else
                  cache_memory[i][j].lru = cache_memory[i][j-1].lru + 1;
         }
      }

      // Initialization of Stream Buffers
      for(int i = 0; i < SB_N_COUNT; i++){
         if (i == 0){
            stream_buffer[i].lru = 0;
            stream_buffer[i].buffer_name = 0;
         }
         else{
            stream_buffer[i].lru = stream_buffer[i-1].lru + 1;
            stream_buffer[i].buffer_name = stream_buffer[i-1].buffer_name + 1;
         }
         for(int j = 0; j < SB_M_BLOCKS; j++){
            stream_buffer[i].buffer[j] = 0;
         }
      }

      // cout << "Cache Memory of size " << sets << "x" << ASSOC << " is created" << endl;  // Debug print to confirm cache is created
      calculateCacheParameters();
   }

   ~Cache() {

      // free up cache memory
      for(int i = 0; i < sets; i++){
         delete[] cache_memory[i];
      }
      delete[] cache_memory;
      // cout << "Deleted the Cache." << endl; // Debug print to confirm cache is deleted

      // free up stream buffers if initialized
      if (SB_N_COUNT != 0){
         for(int i = 0; i < SB_N_COUNT; i++){
            delete[] stream_buffer[i].buffer;
         }
         delete[] stream_buffer;
      }

   }

   void calculateCacheParameters(){
      if(this->ASSOC != 0){
         this->sets = (uint32_t) this->SIZE / (this->ASSOC * this->BLOCKSIZE);
      }
      else{
         this->sets = 0;
      }
      this->index_bits = (unsigned int) log2(this->sets);
      this->blockoffset_bits = (unsigned int) log2(this->BLOCKSIZE);
      this->tag_bits = (unsigned int) this->address_length - this->blockoffset_bits - this->index_bits; 

      // // display if required
      // printf("Cache Parameters : \n");
      // printf("Sets : %d,\tIndexBits : %d,\tBlockOffset : %d,\tTagBits : %d\n\n", this->sets, this->index_bits, this->blockoffset_bits, this->tag_bits);
   }

   void getIndexBlockoffsetTag(uint32_t address){
      index =  (address >> this->blockoffset_bits) & ((1 << this->index_bits) - 1);
      blockoffset =  address & ((1 << this->blockoffset_bits) - 1);
      tag = address >> (this->blockoffset_bits + this->index_bits);

      // printf("Index : %u\n", index);
      // printf("blockoffset : %u\n", blockoffset);
      // printf("tag : %u\n", tag);

   }

   void sortStreamBuffers(){
      // Sort the buffer based on the lru
      sort(stream_buffer, stream_buffer + SB_N_COUNT, [](const stream_buffer_t& a, const stream_buffer_t& b) {return a.lru < b.lru;});      
   }

   // Update the LRU of the Cache set
   void updateLRU(int index, int block){
      int current_lru = cache_memory[index][block].lru;
      for(int j = 0; j < ASSOC; j++){
         if(cache_memory[index][j].lru < current_lru){
               cache_memory[index][j].lru++;
         }
      }
      cache_memory[index][block].lru = 0;
   }

   // Update the LRU of all the buffers and sort it accordingly
   void updateLRU_SB(int buffer_id){
      int current_lru = stream_buffer[buffer_id].lru;
      for(int i = 0; i < SB_N_COUNT; i++){
         if(stream_buffer[i].lru < current_lru){
               stream_buffer[i].lru++;
         }
      }
      stream_buffer[buffer_id].lru = 0;
      sortStreamBuffers(); // sorting every time lru is changed so that while traversing can go from mru to lru
   }

   // Display Cache memory
   void print_cache(){
      for(int i = 0; i < sets; i++){
         cout << "set " << setw(6) << to_string(i) << ": ";
         print_set(i);

         // // Custom Print
         // for(int j = 0; j < ASSOC; j++){
         //     printf("[%u, %x, %u, %u]\t", cache_memory[i][j].valid, cache_memory[i][j].tag, cache_memory[i][j].dirty_bit, cache_memory[i][j].lru);
         // }
         // printf("\n");
      }
   }

   // Print just a set
   void print_set(uint32_t index){
      int lru_count = 0;
      for (int i = 0; i < ASSOC; i++)
      {
         // order according to the mru to lru
         for (int j = 0; j<ASSOC; j++)
         {
               if(cache_memory[index][j].lru == lru_count && cache_memory[index][j].valid == 1){
                  cout << setw(8) <<hex << cache_memory[index][j].tag;
                  if(cache_memory[index][j].dirty_bit == 1){
                     cout << " D";
                  }
                  else{
                     cout << "  ";
                  }
                  // cout << "  ";
                  lru_count++;
               }
         }
      }
      cout << "\n";
   }

   // Display Stream Buffers
   void print_stream_buffers(){
      for(int i = 0; i < SB_N_COUNT; i++){

         // // print Name(Valid) if required
         // cout << "Buffer : " << i << "(" << stream_buffer[i].valid << ") - ";

         if (stream_buffer[i].valid){
            for (int j = 0; j < SB_M_BLOCKS; j++){
               cout << setw(8) << hex << stream_buffer[i].buffer[j];
            }
            cout << endl;
         }
      }
      cout << endl;
   }

   void debug_print_stream_buffers(){
      // Debug Print
      for(int i = 0; i < SB_N_COUNT; i++){
         if (stream_buffer[i].valid){
            cout <<"			SB:  ";
            for (int j = 0; j < SB_M_BLOCKS; j++){
               cout << setw(8) << hex << stream_buffer[i].buffer[j];
            }
            cout << endl;
         }
      }
   }

   // Display Cache Results
   void print_results(){
      printf("Results : \nREADS : %u\nREADMISSES : %u\nWRITES : %u\nWRITEMISSES : %u\nWRITEBACKS : %u\nPREFETCHES : %u\n", READS, READMISSES, WRITES, WRITEMISSES, WRITEBACKS, PREFETCHES);
   }

   // Add Block to the cache if any invalid
   int addBlock(uint32_t address, bool dirty){
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         // Check for Invalid Block
         if(cache_memory[index][j].valid == 0){
               cache_memory[index][j].tag = this->tag;
               cache_memory[index][j].valid = 1;
               cache_memory[index][j].dirty_bit = dirty;
               updateLRU(index,j);
               return 0;
         }
      }
      return 0;
   }


   // Function to service the read request
   int readService(uint32_t address){
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         // Check for the Read hit
         if (tag == cache_memory[this->index][j].tag && cache_memory[this->index][j].valid == 1){
               // cout << "HIT :)" << endl; // Debug print
               updateLRU(this->index, j);
               return 0;
         }

         // Check Stream Buffers for a Prefetch Hit

      }
      // Read Miss
      // cout << "MISS :(" << endl; // Debug print

      // Create a new prefetch stream

      return 1;
   }

   // Function to service the write request
   int writeService(uint32_t address){
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         // printf("Inside WS [%u, %u, %u, %u]\t", cache_memory[this->index][j].valid, cache_memory[this->index][j].tag, cache_memory[this->index][j].dirty_bit, cache_memory[this->index][j].lru);
         // Check for Write hit
         if (tag == cache_memory[this->index][j].tag && cache_memory[this->index][j].valid == 1){
               // cout << "HIT :) setting dirty bit" << endl;
               updateLRU(this->index, j);
               cache_memory[this->index][j].dirty_bit = 1;
               return 0;
         }
      }
      // Write miss
      // cout << "MISS :(" << endl;
      return 1; // 
   }

   // Function to check dirty bit
   uint32_t checkWriteBack(uint32_t address){
      uint32_t dirtyblock;
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         // if any of the block is invalid (empty), eviction is not required
         if(cache_memory[this->index][j].valid == 0){
               return 0;
         }
      }
      for(int j = 0; j < ASSOC; j++){
         // if LRU is dirty, eviction with writeBack is necessary
         if (cache_memory[this->index][j].lru == (ASSOC-1) && cache_memory[this->index][j].dirty_bit == 1){
               // cout << "LRU is Dirty. Writeback required" << endl; // Debug Print to confirm Dirty block is found
               dirtyblock = (uint32_t)(cache_memory[this->index][j].tag << (index_bits+blockoffset_bits)) + (index << blockoffset_bits) + (blockoffset);
               return dirtyblock;
         }
      }
      return 0;
   }

   // Function to check clean eviction
   uint32_t checkEviction(uint32_t address){
      uint32_t evictblock;
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         // if any of the block is invalid (empty), eviction is not required
         if(cache_memory[this->index][j].valid == 0){
               return 0;
         }
      }
      for(int j = 0; j < ASSOC; j++){
         // if LRU is dirty, eviction with writeBack is necessary
         if (cache_memory[this->index][j].lru == (ASSOC-1)){
               // cout << "LRU is Clean. Writeback NOT required" << endl;  // Debug Print to confirm Clean block is found;
               evictblock = (uint32_t)(cache_memory[this->index][j].tag << (index_bits+blockoffset_bits)) + (index << blockoffset_bits) + (blockoffset);
               return evictblock;
         }
      }
      return 0;
   }

   // Perform eviction
   void performEviction(uint32_t address){
      this->getIndexBlockoffsetTag(address);
      for(int j = 0; j < ASSOC; j++){
         if(cache_memory[this->index][j].tag == this->tag){
               cache_memory[this->index][j].tag = 0;
               cache_memory[this->index][j].valid = 0;  // invalidate to add the block
               cache_memory[this->index][j].dirty_bit = 0; // make it clean
         }
      }
   }

   // Function to return invalid (if available) or LRU Buffer to Add the new buffers 
   int CheckForBufferToAdd(){

      // Sort the Buffers before find the required buffer
      sortStreamBuffers();

      // return invalid buffer if exists
      for (int i = 0; i < SB_N_COUNT; i++){
         if(stream_buffer[i].valid == 0){
            return stream_buffer[i].buffer_name;
         }
      }
      
      // else return last buffer which essentially LRU
      return stream_buffer[SB_N_COUNT-1].buffer_name;
   }

   // Adds new blocks to the Stream Buffer after Stream Buffer Miss (No need to flush as we will be replacing)
   void AddNewBlocksToSB(int buffer_name, uint32_t address){
      this->getIndexBlockoffsetTag(address);
      uint32_t blockAddress = (uint32_t) (address >> blockoffset_bits);
      for (int i = 0; i < SB_N_COUNT; i++){
         if(stream_buffer[i].buffer_name == buffer_name){
            stream_buffer[i].valid = true;
            for(int j=0; j<SB_M_BLOCKS; j++){
               stream_buffer[i].buffer[j] = blockAddress+j+1;
               this->PREFETCHES++;
               MEMORY_TRAFFIC++;
            }
            // this->PREFETCHES += SB_M_BLOCKS;
            // MEMORY_TRAFFIC += SB_M_BLOCKS;

            updateLRU_SB(i);
         }
      }
   }

   int ModifyStreamBuffer(uint32_t blockAddress, int buffer_name, int matching_block){
      // bool print = false; 
      // if(blockAddress == 64494196){
      //    print = true;            
      //    }   // Used for debugging a boundary condition issue. Kept it incase required
      // erase the above blocks and shift the remaining blocks up
      for (int i = 0; i < SB_N_COUNT; i++){

         if(stream_buffer[i].buffer_name == buffer_name){

            // Used for debugging a boundary condition issue. Kept it incase required
            // if (print){
            //    cout << "before erase. buffer_name : " << buffer_name << endl;
            //    debug_print_stream_buffers();
            // }

            // Erase the required blocks
            int j = 0, k = 0;
            uint32_t last_buffer = blockAddress;
            do {
               stream_buffer[i].buffer[j] = 0;
               j++;
            }
            while(j <= matching_block);

            // Used for debugging a boundary condition issue. Kept it incase required
            // if (print){
            //    cout << "after erase. buffer_name : " << buffer_name << endl;
            //    debug_print_stream_buffers();
            // }

            // cout << "Buffer after erasing" << endl;
            // debug_print_stream_buffers(); // Print to check the stream buffer after erasing

            // shift the buffer
            // cout << "Current row in buffer" << j << endl;
            while(j < SB_M_BLOCKS){
               stream_buffer[i].buffer[k] = stream_buffer[i].buffer[j];
               last_buffer = stream_buffer[i].buffer[j];
               k++;
               j++;
            }

            // Used for debugging a boundary condition issue. Kept it incase required
            // if (print){
            //    cout << "after shift. buffer_name : " << buffer_name << endl;
            //    debug_print_stream_buffers();
            // }

            // Add next blocks
            int add = 1;
            while(k < SB_M_BLOCKS){
               stream_buffer[i].buffer[k] = last_buffer+add;
               this->PREFETCHES++;
               MEMORY_TRAFFIC++;
               k++;
               add++;
            }

            // Used for debugging a boundary condition issue. Kept it incase required
            // if (print){
            //    cout << "after addition. buffer_name : " << buffer_name << endl;
            //    debug_print_stream_buffers();
            // }
            // debug_print_stream_buffers(); // Print after complete modification
            return 0;
         }
      }
      return 0;
   }

   // Traverse and Check Prefetch
   int CheckStreamBuffer(uint32_t address){
      this->getIndexBlockoffsetTag(address);
      uint32_t blockAddress = (uint32_t) (address >> blockoffset_bits);

      // Sort the Buffers based on LRU
      sortStreamBuffers();

      // Traverse the Buffers from beginning
      for (int i = 0; i < SB_N_COUNT; i++){
         for (int j =0; j < SB_M_BLOCKS; j++){
            if(blockAddress == stream_buffer[i].buffer[j]){

               // Modify the StreamBuffer. Below are the functionalities performed by Modify
               // 1. Re-arrange the blocks according to the hit
               // 2. Fetch the new block and add to the buffer 
               // (as buffer is present only on the last cache level, fetching from new block is equivalent to adding directly)

               ModifyStreamBuffer(blockAddress, stream_buffer[i].buffer_name, j);

               // Update the LRU
               updateLRU_SB(i);

               return 1; // Return 1 if its stream buffer hit
            }
         }
      }
      return 0;
   }
};


int CacheController(char rw, uint32_t addr, MemoryBlock* current, bool recursive){
   int code; // return value of service
   
   // cout << endl << "Current cache level : " << current->name << endl;
   
   // cout << rw << " --- " << addr << " , Level : " << current->name << endl;
   uint32_t index =  (addr >> current->data->blockoffset_bits) & ((1 << current->data->index_bits) - 1);
   // uint32_t blockoffset =  addr & ((1 << current->data->blockoffset_bits) - 1);
   uint32_t tag = addr >> (current->data->blockoffset_bits + current->data->index_bits);
   uint32_t addr_wo_blockoffset = addr&(~((1 << current->data->blockoffset_bits) - 1));

   // cout << index << ", " << blockoffset << ", " << tag << endl;

   // current->data->getIndexBlockoffsetTag(addr);
   // cout << current->next->name << endl;
   // current->next->data->getIndexBlockoffsetTag(addr);
   
   if(DEBUG_PRINT){
      // Cache Level Indentation depending on level
      for(int i=0; i<current->name; i++){printf("\t");}
      // print tag and index in required format
      printf("L%d: %c %x (tag=%x index=%u)\n", current->name, rw, addr, tag, index);

      // Cache set contents print before performing cache operation 
      for(int i=0; i<current->name; i++){printf("\t");}
      // print the set before
      printf("L%d: before: set     %u: ", current->name, index);
      // print cache set contents
      current->data->print_set(index);
   }

   if (rw == 'r'){
      current->data->READS++;
      // Check for hit
      code = current->data->readService(addr);
      if(code == 0){
         // Check if its a prefetcher hit. 
         if(current->data->SB_N_COUNT > 0) {
            // CheckStreamBuffer will take care of modifying the Stream Buffers as required.
            int sb_hit = current->data->CheckStreamBuffer(addr);  // sb_hit (0-Miss in SB, 1-Hit in SB)

            if(sb_hit){

            }
         }
      }

      // Handle a miss
      else{
         if(current->data->SB_N_COUNT == 0) {
            current->data->READMISSES++;
         }
         // Check dirty bit and perform writeback if required
         uint32_t dirtyblock = current->data->checkWriteBack(addr);

         if(dirtyblock){
               // Perform WB only if its not last level cache
               if(current->next != nullptr){
                  CacheController('w', dirtyblock, current->next, true);
               }
               
               // Perform eviction
               current->data->performEviction(dirtyblock);

               current->data->WRITEBACKS++;
               
               if(current->next == nullptr){
                  MEMORY_TRAFFIC++;
               }
         }
         else{
               // check for clean eviction
               uint32_t evictblock = current->data->checkEviction(addr);

               if(evictblock){
                  // Perform eviction
                  current->data->performEviction(evictblock);
               }
         }

         // Check in Prefetch if its available before going to next level
         if(current->data->SB_N_COUNT > 0) {
            // Traverse through the stream buffers based on recency and check for hit
            int sb_hit = current->data->CheckStreamBuffer(addr);  // sb_hit (0-Miss in SB, 1-Hit in SB)

            // Miss in C and Hit in SB - Prefetch miss 
            if(sb_hit){

               // Do nothing. Cache will take care of modifying
               // cout << "********Miss in Cache and Hit in SB********" << endl;
            }
            else{
               current->data->READMISSES++;
               MEMORY_TRAFFIC++;
               // cout << "********Miss in Cache and Miss in SB********" << endl;

               int buffer_name = 0;
               // Find the Invalid/LRU Stream Buffer to add
               buffer_name = current->data->CheckForBufferToAdd();

               // Prefetch New Blocks and add it to the Stream Buffer
               current->data->AddNewBlocksToSB(buffer_name, addr);
            }  
         }

         // Last memory level
         if (current->next == nullptr){
               // cout << "Getting Block from MEM" << endl;
               current->data->addBlock(addr, false);
               if(current->data->SB_N_COUNT == 0) {
                  MEMORY_TRAFFIC++;
               }
            if(DEBUG_PRINT){
               // Cache set contents print before performing cache operation 
               for(int i=0; i<current->name; i++){printf("\t");}
               // print the set before
               printf("L%d:  after: set     %u: ", current->name, index);
               // print cache set contents
               current->data->print_set(index);
               current->data->debug_print_stream_buffers();
            }
            return 0;
         }
         CacheController('r', addr_wo_blockoffset, current->next, true);
         current->data->addBlock(addr, false);
      }        
   }
      
   else if (rw == 'w'){
      current->data->WRITES++;
      // Check for hit
      code = current->data->writeService(addr);
      if(code == 0){
         // Check if its a prefetcher hit. 
         if(current->data->SB_N_COUNT > 0) {
            // CheckStreamBuffer will take care of modifying the Stream Buffers as required.
            current->data->CheckStreamBuffer(addr);  // sb_hit (0-Miss in SB, 1-Hit in SB)
         }
      }

      // Handle miss
      else{
         if(current->data->SB_N_COUNT == 0) {
            current->data->WRITEMISSES++;
         }

         // Check dirty bit and perform writeback if required
         uint32_t dirtyblock = current->data->checkWriteBack(addr);

         if(dirtyblock){
               
               // Perform WB only if its not last level cache
               if(current->next != nullptr){
                  CacheController('w', dirtyblock, current->next, true);
               }
               // Perform eviction
               current->data->performEviction(dirtyblock);

               current->data->WRITEBACKS++;

               if(current->next == nullptr){
                  MEMORY_TRAFFIC++;
               }
         }
         else{
               // check for clean eviction
               uint32_t evictblock = current->data->checkEviction(addr);

               if(evictblock){
                  // Perform eviction
                  current->data->performEviction(evictblock);
               }
         }

         // Check in Prefetch if its available before going to next level
         if(current->data->SB_N_COUNT > 0) {
            // Traverse through the stream buffers based on recency and check for hit
            int sb_hit = current->data->CheckStreamBuffer(addr);  // sb_hit (0-Miss in SB, 1-Hit in SB)

            // Miss in C and Hit in SB - Prefetch miss 
            if(sb_hit){
               // Do nothing. Cache will take care of modifying
               // cout << "********Miss in Cache and Hit in SB********" << endl;
            }
            else{
               current->data->WRITEMISSES++;
               MEMORY_TRAFFIC++;

               int buffer_name = 0;
               // Find the Invalid/LRU Stream Buffer to add
               buffer_name = current->data->CheckForBufferToAdd();

               // Prefetch New Blocks and add it to the Stream Buffer
               current->data->AddNewBlocksToSB(buffer_name, addr);
            }  
         }

         // Last memory level
         if (current->next == nullptr){
               // cout << "Getting Block from MEM" << endl;
               current->data->addBlock(addr, true);
               
               if(current->data->SB_N_COUNT == 0) {
                  MEMORY_TRAFFIC++;
               }
               
            if(DEBUG_PRINT){
               // Cache set contents print before performing cache operation 
               for(int i=0; i<current->name; i++){printf("\t");}
               // print the set before
               printf("L%d:  after: set     %u: ", current->name, index);
               // print cache set contents
               current->data->print_set(index);
               // current->data->debug_print_stream_buffers(); 
            }
            return 0;
         }

         CacheController('r', addr_wo_blockoffset, current->next, true);
         current->data->addBlock(addr, true);
      }
   }
   else {
      printf("Error: Unknown request type %c.\n", rw);
      exit(EXIT_FAILURE);
   }

   if(DEBUG_PRINT){
      // Cache set contents print before performing cache operation 
      for(int i=0; i<current->name; i++){printf("\t");}
      // print the set before
      printf("L%d:  after: set     %u: ", current->name, index);
      // print cache set contents
      current->data->print_set(index);
      current->data->debug_print_stream_buffers(); 
   }

   return 0;
}


int main(int argc, char *argv[]){

   FILE *fp;			
   char *trace_file;		
   cache_params_t params;	
   char rw;	
   uint32_t addr;
   int err = 0;
   float MissRate = 0.00f;

   // Exit with an error if the number of command-line arguments is incorrect.
   if (argc != 9) {
      printf("Error: Expected 8 command-line arguments but was provided %d.\n", (argc - 1));
      exit(EXIT_FAILURE);
   }
   
   // "atoi()" (included by <stdlib.h>) converts a string (char *) to an integer (int).
   params.BLOCKSIZE = (uint32_t) atoi(argv[1]);
   params.L1_SIZE   = (uint32_t) atoi(argv[2]);
   params.L1_ASSOC  = (uint32_t) atoi(argv[3]);
   params.L2_SIZE   = (uint32_t) atoi(argv[4]);
   params.L2_ASSOC  = (uint32_t) atoi(argv[5]);
   params.PREF_N    = (uint32_t) atoi(argv[6]);
   params.PREF_M    = (uint32_t) atoi(argv[7]);
   trace_file       = argv[8];
   
   // Open the trace file for reading.
   fp = fopen(trace_file, "r");
   if (fp == (FILE *) NULL) {
      // Exit with an error if file open failed.
      printf("Error: Unable to open file %s\n", trace_file);
      exit(EXIT_FAILURE);
   }

   // Print simulator configuration.
   printf("===== Simulator configuration =====\n");
   printf("BLOCKSIZE:  %u\n", params.BLOCKSIZE);
   printf("L1_SIZE:    %u\n", params.L1_SIZE);
   printf("L1_ASSOC:   %u\n", params.L1_ASSOC);
   printf("L2_SIZE:    %u\n", params.L2_SIZE);
   printf("L2_ASSOC:   %u\n", params.L2_ASSOC);
   printf("PREF_N:     %u\n", params.PREF_N);
   printf("PREF_M:     %u\n", params.PREF_M);
   printf("trace_file: %s\n", trace_file);
   printf("\n");

   // // Read requests from the trace file and echo them back.
   // while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {	// Stay in the loop if fscanf() successfully parsed two tokens as specified.
   //     if (rw == 'r')
   //         printf("r %x\n", addr);
   //     else if (rw == 'w')
   //         printf("w %x\n", addr);
   //     else {
   //         printf("Error: Unknown request type %c.\n", rw);
   //         exit(EXIT_FAILURE);
   //     }
   // }


   // Initialize the Cache memory depending on the size - currently not configurable
   
   if(params.L2_SIZE){
      Cache L1_cache(params.BLOCKSIZE, params.L1_SIZE, params.L1_ASSOC, 0, 0);
      Cache L2_cache(params.BLOCKSIZE, params.L2_SIZE, params.L2_ASSOC, params.PREF_N, params.PREF_M);

      // // Print Cache Parameters
      // printf("L1 Cache Parameters : \n");
      // L1_cache.calculateCacheParameters();
      // printf("\n\n");
      // printf("L2 Cache Parameters : \n");
      // L2_cache.calculateCacheParameters();

      // // Print initialized cache if necessary
      // printf("L1 Cache contents : \n");
      // L1_cache.print_cache();
      // printf("\n\n");
      // printf("L2 Cache contents : \n");
      // L2_cache.print_cache();

      // Create a Linked List depending on the Levels of Cache
      // This Linked List will represent the whole Memory
      Memory memory_list;
      memory_list.insertNewCache(&L1_cache, 1);
      memory_list.insertNewCache(&L2_cache, 2);
      // memory_list.displayMemory();
      
      // Once the whole memory is initialized, create a controller function
      // This controller function parses the address one by one and performs the cache simulation

      while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {
         parse_count++;

         if(DEBUG_PRINT)
               printf("%d=%c %x\n", parse_count, rw, addr);

         CacheController(rw, addr, memory_list.head, false);

         // // Print cache to check every parse - Debug Purpose
         // printf("\nL1 Cache contents : \n");
         // L1_cache.print_cache();
         // printf("\n\n");

         // printf("\nL2 Cache contents : \n");
         // L2_cache.print_cache();
         // printf("\n\n");
      }

      // Print cache at the end
      printf("===== L1 contents =====\n");
      L1_cache.print_cache();
      printf("\n");
      // L1_cache.print_results();
      // printf("\n\n");

      printf("===== L2 contents =====\n");
      L2_cache.print_cache();
      printf("\n");
      // L2_cache.print_results();
      // printf("\n\n");

      if (params.PREF_N != 0) {
         printf("===== Stream Buffer(s) contents =====\n");
         L2_cache.print_stream_buffers();
      }

      printf("===== Measurements =====\n");
      printf("a. L1 reads:                   %d\n", L1_cache.READS);
      printf("b. L1 read misses:             %d\n", L1_cache.READMISSES);
      printf("c. L1 writes:                  %d\n", L1_cache.WRITES);
      printf("d. L1 write misses:            %d\n", L1_cache.WRITEMISSES);
      MissRate = (float)(L1_cache.READMISSES + L1_cache.WRITEMISSES)/(L1_cache.READS + L1_cache.WRITES);
      printf("e. L1 miss rate:               %.4f\n", MissRate);
      printf("f. L1 writebacks:              %d\n", L1_cache.WRITEBACKS);
      printf("g. L1 prefetches:              %d\n", L1_cache.PREFETCHES);
      printf("h. L2 reads (demand):          %d\n", L2_cache.READS);
      printf("i. L2 read misses (demand):    %d\n", L2_cache.READMISSES);
      printf("j. L2 reads (prefetch):        %d\n", L2_cache.PREFETCHREADS);
      printf("k. L2 read misses (prefetch):  %d\n", L2_cache.PREFETCHMISSES);
      printf("l. L2 writes:                  %d\n", L2_cache.WRITES);
      printf("m. L2 write misses:            %d\n", L2_cache.WRITEMISSES);
      MissRate = (float)(L2_cache.READMISSES)/(L2_cache.READS);
      printf("n. L2 miss rate:               %.4f\n", MissRate);
      printf("o. L2 writebacks:              %d\n", L2_cache.WRITEBACKS);
      printf("p. L2 prefetches:              %d\n", L2_cache.PREFETCHES);
      printf("q. memory traffic:             %d\n", MEMORY_TRAFFIC);
   }

   else {
      Cache L1_cache(params.BLOCKSIZE, params.L1_SIZE, params.L1_ASSOC, params.PREF_N, params.PREF_M);

      // // Print Cache Parameters
      // printf("L1 Cache Parameters : \n");
      // L1_cache.calculateCacheParameters();
      // printf("\n\n");

      // // Print initialized cache if necessary
      // printf("L1 Cache contents : \n");
      // L1_cache.print_cache();
      // printf("\n\n");

      // Create a Linked List depending on the Levels of Cache
      // This Linked List will represent the whole Memory
      Memory memory_list;
      memory_list.insertNewCache(&L1_cache, 1);
      // memory_list.displayMemory();
      
      // Once the whole memory is initialized, create a controller function
      // This controller function parses the address one by one and performs the cache simulation

      while (fscanf(fp, "%c %x\n", &rw, &addr) == 2) {
            parse_count++;

            if(DEBUG_PRINT)
               printf("%d=%c %x\n", parse_count, rw, addr);

            CacheController(rw, addr, memory_list.head, false);

            // // Print cache to check every parse - Debug Purpose
            // printf("\nL1 Cache contents : \n");
            // L1_cache.print_cache();
            // printf("\n\n");
      }

      // Print cache at the end
      printf("===== L1 contents =====\n");
      L1_cache.print_cache();
      printf("\n");
      // L1_cache.print_results();
      // printf("\n\n");

      if (params.PREF_N != 0) {
         printf("===== Stream Buffer(s) contents =====\n");
         L1_cache.print_stream_buffers();
      }


      printf("===== Measurements =====\n");
      printf("a. L1 reads:                   %d\n", L1_cache.READS);
      printf("b. L1 read misses:             %d\n", L1_cache.READMISSES);
      printf("c. L1 writes:                  %d\n", L1_cache.WRITES);
      printf("d. L1 write misses:            %d\n", L1_cache.WRITEMISSES);
      MissRate = (float)(L1_cache.READMISSES + L1_cache.WRITEMISSES)/(L1_cache.READS + L1_cache.WRITES);
      printf("e. L1 miss rate:               %.4f\n", MissRate);
      printf("f. L1 writebacks:              %d\n", L1_cache.WRITEBACKS);
      printf("g. L1 prefetches:              %d\n", L1_cache.PREFETCHES);
      printf("h. L2 reads (demand):          %d\n", 0);
      printf("i. L2 read misses (demand):    %d\n", 0);
      printf("j. L2 reads (prefetch):        %d\n", 0);
      printf("k. L2 read misses (prefetch):  %d\n", 0);
      printf("l. L2 writes:                  %d\n", 0);
      printf("m. L2 write misses:            %d\n", 0);
      printf("n. L2 miss rate:               %.4f\n", 0.00f);
      printf("o. L2 writebacks:              %d\n", 0);
      printf("p. L2 prefetches:              %d\n", 0);
      printf("q. memory traffic:             %d\n", MEMORY_TRAFFIC);
      }

   return err;
}