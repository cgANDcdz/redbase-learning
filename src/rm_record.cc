//
// File:        rm_record.cc
// Description: RM_Record class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//

#include<cstring>
#include "rm.h"
#include "rm_rid.h"
using namespace std;

// Default constructor
RM_Record::RM_Record() {
    isValid=false;
}

// Destructor
RM_Record::~RM_Record() {
    if(isValid){
        isValid=false;
        delete [] pRecData;
    }
}

RC RM_Record::SetMembers(char* pRecordData,RID recordId,int recordSize){
    rid=recordId;
    recSize=recordSize;
    pRecData=new char[recSize];
    memcpy(pRecData,pRecordData,recSize);
    isValid=true;
    return OK_RC;
}

// Return the data corresponding to the record
RC RM_Record::GetData(char *&pData) const {
    if(!isValid){
        return RM_REC_NOT_VALID;
    }
    pData=pRecData;
    return OK_RC;
}

// Return the RID associated with the record
RC RM_Record::GetRid (RID &rid) const {
    if(!isValid){
        return RM_REC_NOT_VALID;
    }
    rid=this->rid;
    return OK_RC;
}


/* 获取记录大小(字节数)*/
RC RM_Record::GetRecSize(int& recordSize) const{
    if(!isValid){
        return RM_REC_NOT_VALID;
    }
    recordSize=recSize;

    return OK_RC;
}