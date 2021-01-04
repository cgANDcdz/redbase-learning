//
// File:        rm_rid.cc
// Description: RID class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//

#include "rm_rid.h"
#include "rm.h"
using namespace std;

// Default constructor
RID::RID() {
    isValid=false;
}

// Constructor with PageNum and SlotNum given
RID::RID(PageNum pageNum, SlotNum slotNum) {
    this->pageNum=pageNum;
    this->slotNum=slotNum;
    isValid=true;
}

// Destructor
RID::~RID() {
    // Don't need to do anything
}


// Return page number
/* 此处const的理解:表示成员函数隐含传入的this指针为const指针
 => 决定了在该成员函数中,任意修改它所在的类的成员的操作都是不允许的 */
RC RID::GetPageNum(PageNum &pageNum) const {
    if(!isValid) return RM_RID_NOT_VALID;
    pageNum=this->pageNum;
    return OK_RC;
}

// Return slot number
RC RID::GetSlotNum(SlotNum &slotNum) const {
    if(!isValid) return RM_RID_NOT_VALID;
    slotNum=this->slotNum;
    return OK_RC;
}

RC RID::SetMembers(PageNum pageNum, SlotNum slotNum){
    this->pageNum=pageNum;
    this->slotNum=slotNum;
    return OK_RC;
}