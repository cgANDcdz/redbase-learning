//
// rm_rid.h
//
//   The Record Id interface
//

#ifndef RM_RID_H
#define RM_RID_H

// We separate the interface of RID from the rest of RM because some
// components will require the use of RID but not the rest of RM.

#include "redbase.h"

//
// PageNum: uniquely identifies a page in a file
//
typedef int PageNum;

//
// SlotNum: uniquely identifies a record in a page
//
typedef int SlotNum;

//
// RID: Record id interface
//
class RID {
public:
    RID();                                         // Default constructor
    RID(PageNum pageNum, SlotNum slotNum);
    ~RID();                                        // Destructor

    RC GetPageNum(PageNum &pageNum) const;         // Return page number
    RC GetSlotNum(SlotNum &slotNum) const;         // Return slot number

    /*自定义,设置成员变量*/
    RC SetMembers(PageNum pageNum, SlotNum slotNum);

private:
    PageNum pageNum;    /* 记录所在的页号 */
    SlotNum slotNum;    /* 记录在页内的位置,本人的实现中,统一从0开始算!!! */
    bool isValid;       /* 是否填充了有效数据*/
};

#endif
