//
// File:        rm_filescan.cc
// Description: RM_FileScan class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//
#include<string.h>
#include "rm.h"
using namespace std;

// Default constructor
RM_FileScan::RM_FileScan() {
    bScanOpen=false;
}

// Destructor
RM_FileScan::~RM_FileScan() {

}

// Initialize a file scan
RC RM_FileScan::OpenScan(const RM_FileHandle &fileHandle, AttrType attrType, int attrLength,
                         int attrOffset, CompOp compOp, void *value, ClientHint pinHint) { /*ClientHint pinHint = NO_HINT*/
    if(bScanOpen){
        return RM_SCAN_ALREADY_OPEN;
    }
    
    /*各种参数检查*/
    // 还应该检查fileHandle、attrOffset,只是暂时没有
    if(attrType!=INT && attrType!=FLOAT && attrType!=STRING){
        return RM_SCAN_INVALID_TYPE;
    }
    if(attrLength<=0){
        return RM_SCAN_ATTR_TOO_SHORT;
    }
    if(compOp!=NO_OP && compOp!=EQ_OP && compOp!=NE_OP && compOp!=LT_OP
        && compOp!=GT_OP && compOp!=LE_OP && compOp!=GE_OP){
            return RM_SCAN_INVALID_OP;
    }
    if(value==NULL){
        return RM_SACAN_VAL_NULL;
    }

    /* 1. 暂存扫描参数/条件*/
    /**
     * 注:对下面fileHandle的获取,直接将引用传递给此类的引用不行,因为输入的参数有const修饰;
     *    但是直接对引用取地址再复制给指针也报错 => 解决方式是const_cast
     *    参考:https://www.cnblogs.com/ider/archive/2011/07/22/cpp_cast_operator_part2.html
     * */
    this->fileHandle=const_cast<RM_FileHandle*>(&fileHandle);
    this->attrType=attrType;
    this->attrLength=attrLength;
    this->attrOffset=attrOffset;
    this->compOp=compOp;
    this->value=value;
    this->pinHint=pinHint;

    /** 2.找到RM层第一个页的信息,从而初始化currPageNum和currSlotNum
     *  => 似乎有点多余,直接将currPageNum初始化为2即可
     **/
    /* 2.1 从RM_FileHandle中获取PF_FileHandle的指针*/
    // PF_FileHandle* pfFileHandle;
    // this->fileHandle->GetPpfFileHandle(pfFileHandle);

    // /* 2.2 通过PF_FileHandle获取文件的第一个page; 这个page是PF层的文件头,获取它只是为了得到它的下一页rmHdrPgNum*/
    // PF_PageHandle pfPageHandle;
    // pfFileHandle->GetFirstPage(pfPageHandle);                       /*需要手动unpin*/
    // PageNum pfHdrPgNum;
    // pfPageHandle.GetPageNum(pfHdrPgNum);

    // /*2.3 获取RM层的文件头信息,它是pfHdrPgNum的下一页*/
    // PageNum rmHdrPgNum;
    // RC rc=pfFileHandle->GetNextPage(pfHdrPgNum,pfPageHandle);    /*注意,这时pfPageHandle对应RM层的头; GetNextPage需要手动释放*/
    // if(rc==PF_EOF){                                              /*此时PF层只有一个page,即头信息*/
    //     rmHdrPgNum=RM_PAGE_LIST_END;
    // }
    // pfPageHandle.GetPageNum(rmHdrPgNum);

    // /* 2.4 初始化currPageNum 和 currSlotNum*/
    // this->currPageNum=rmHdrPgNum;
    // this->currSlotNum=0;

    // /*2.4 unpin缓冲区中的page*/
    // pfFileHandle->UnpinPage(pfHdrPgNum);
    // pfFileHandle->UnpinPage(rmHdrPgNum);

    this->currPageNum=2;            /*0是PF_FileHdr、 1是RM_FileHdr => 个人暂时这么理解*/
    this->currSlotNum=0;
    
    this->bScanOpen=true;
    return OK_RC;
}

// Get next matching record
RC RM_FileScan::GetNextRec(RM_Record &rec) {
    if(!bScanOpen){
        return RM_SCAN_NOT_OPEN;
    }

    /* 1.获取PF层的PF_Filehandle,方便对page进行相关控制*/
    PF_FileHandle* pfFileHandle;
    fileHandle->GetPpfFileHandle(pfFileHandle);

    /* 2.当前文件相关信息*/
    int numPages=fileHandle->GetRmNumPages(numPages);
    int totalPageNum=numPages+2;                       /*因为实际还有:PF_FileHdr和RM_FileHdr*/
    int numSlots;
    fileHandle->GetPageSlots(numSlots);                /*一个page能存储的页数*/

    /* 3.查找记录*/
    PF_PageHandle pageHandle;
    char* pPageData;
    char* pRecData;
    for(int page=currPageNum;page<totalPageNum;page++){

        pfFileHandle->GetThisPage(page,pageHandle);         /*需要手动unpin*/
        pageHandle.GetData(pPageData);

        for(int slot=currSlotNum;slot<numSlots;slot++){
            if(!fileHandle->IsSlotUsed(pPageData,slot))     /*未占用*/
                continue;   
            fileHandle->GetSlotData(pageHandle,slot,pRecData);
            char* attr=pRecData+attrOffset;
            if(IsMatch(attr)){                            /*符合查找条件*/
                RID rid(page,slot);
                fileHandle->GetRec(rid,rec);
                if(slot+1==numSlots){currSlotNum=0; currPageNum++;}
                else currSlotNum+=1;
                pfFileHandle->UnpinPage(page);
                return OK_RC;
            }    
        }

        pfFileHandle->UnpinPage(page);
        /* 当前页没有找到,查找下一页*/
        currPageNum++;                  /*要查找下一页,currPageNum随之增加*/
        currSlotNum=0;
    }

    return RM_EOF;
}

// Close the scan
RC RM_FileScan::CloseScan() {
    if(!bScanOpen){
        return RM_SCAN_NOT_OPEN;
    }
    bScanOpen=false;
    return OK_RC;
}



/*数据库中某个rec的属性值attr 与 value是否符合条件compOp*/
bool RM_FileScan::IsMatch(char* attr){

    /**
     * 由于字符数据没有填充`\0`,不能使用strcmp => 改用strcnmp
     * */
    switch (compOp)
    {
        case NO_OP:                         /*不比较*/
            return true;                
            break;
            
        case EQ_OP:                         /*attr==val*/
            if(attrType==INT)         return *((int*)attr) == *((int*)value);
            else if(attrType==FLOAT)  return *((float*)attr) == *((float*)value);
            else if(attrType==STRING) return  strncmp(attr,(char*)value,attrLength)==0; 
            break;

        case NE_OP:                         /*attr!=val*/
            if(attrType==INT)         return !(*((int*)attr) == *((int*)value));
            else if(attrType==FLOAT)  return !(*((float*)attr) == *((float*)value));
            else if(attrType==STRING) return  !(strncmp(attr,(char*)value,attrLength)==0); 
            break;

        case LT_OP:                         /*attr<val*/
            if(attrType==INT)         return *((int*)attr) < *((int*)value);
            else if(attrType==FLOAT)  return *((float*)attr) < *((float*)value);
            else if(attrType==STRING) return  strncmp(attr,(char*)value,attrLength)<0; 
            break;

        case GT_OP:                         /*attr>val*/
            if(attrType==INT)         return *((int*)attr) > *((int*)value);
            else if(attrType==FLOAT)  return *((float*)attr) > *((float*)value);
            else if(attrType==STRING) return  strncmp(attr,(char*)value,attrLength)>0; 
            break;

        case LE_OP:                         /*attr<=val*/
            if(attrType==INT)         return *((int*)attr) <= *((int*)value);
            else if(attrType==FLOAT)  return *((float*)attr) <= *((float*)value);
            else if(attrType==STRING) return  strncmp(attr,(char*)value,attrLength)<=0; 
            break;

        case GE_OP:                         /*attr>=val*/
            if(attrType==INT)         return *((int*)attr) >= *((int*)value);
            else if(attrType==FLOAT)  return *((float*)attr) >= *((float*)value);
            else if(attrType==STRING) return  strncmp(attr,(char*)value,attrLength)>=0; 
            break;

        default:
            return false;
            break;
    }

    /*不会达到这里*/
    return true;
}