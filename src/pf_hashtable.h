//
// File:        pf_hashtable.h
// Description: PF_HashTable class interface
// Authors:     Hugo Rivero (rivero@cs.stanford.edu)
//              Dallan Quass (quass@cs.stanford.edu)
//

#ifndef PF_HASHTABLE_H
#define PF_HASHTABLE_H

#include "pf_internal.h"

//
// HashEntry - Hash table bucket entries 
//=> hashtable的一个bucket对应一个双向链表,PF_HashEntry是一个链表项
struct PF_HashEntry {
    PF_HashEntry *next;   // next hash table element or NULL
    PF_HashEntry *prev;   // prev hash table element or NULL
    int          fd;      // file descriptor
    PageNum      pageNum; // page number
    int          slot;    // slot of this page in the buffer,它在缓冲区中的编号
};

//
// PF_HashTable - allow search, insertion, and deletion of hash table entries
//
class PF_HashTable {
public:
    PF_HashTable (int numBuckets);           // Constructor
    ~PF_HashTable();                         // Destructor
    RC  Find     (int fd, PageNum pageNum, int &slot);
                                             // Set slot to the hash table
                                             // entry for fd and pageNum
    RC  Insert   (int fd, PageNum pageNum, int slot);
                                             // Insert a hash table entry
    RC  Delete   (int fd, PageNum pageNum);  // Delete a hash table entry

private:
    // Hash function:(fd + pageNum) % numBuckets
    int Hash (int fd, PageNum pageNum) const {
         return ((fd + pageNum) % numBuckets); 
    }   


    int numBuckets;                               // Number of hash table buckets
    PF_HashEntry **hashTable;                     // Hash table => 看做一个数组,输出每一项是一个双向链表
};

#endif
