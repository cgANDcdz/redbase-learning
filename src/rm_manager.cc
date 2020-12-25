//
// File:        rm_manager.cc
// Description: RM_Manager class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//

#include<cstring>
#include "rm.h"
using namespace std;

/**********************************************************************************
 *                                  RM_Manager的实现
 * 1.cpp可以不使用new创建对象:PF_FileHandle pfFileHandle;这种方式会自动垃圾回收!
 * 2.遇到错误时,如果是调用PF层的错误,统一返回RM_PF;如果是RM层错误,返回自定义编号
 * 3.
 * ********************************************************************************/


// Constructor
RM_Manager::RM_Manager(PF_Manager &pfm){
    this->pfManager=&pfm;           /*指针!*/                   
}

// Destructor
RM_Manager::~RM_Manager() {

}


// Create a file with the given filename and record size,并且写RM层的文件头
RC RM_Manager::CreateFile(const char *fileName, int recordSize) {
    if(recordSize<=0){
        return RM_RECORD_TOO_SMALL;
    }
    if(recordSize > PF_PAGE_SIZE-sizeof(RM_PageHdr)){   /*注意减去页头*/
        return RM_RECORD_TOO_BIG;
    }
    if(fileName==NULL){
        return RM_INVALID_FILENAME;
    }

     /* 1.创建文件(PF层,内部会自动添加PF层头信息) */   
    RC rc=pfManager->CreateFile(fileName);        
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 2.打开文件; 将文件与pfFileHandle关联*/
    PF_FileHandle pfFileHandle;
    rc=pfManager->OpenFile(fileName,pfFileHandle); 
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 3.为文件分配第一个page(RM层的第一个page,存储RM文件头)*/
    PF_PageHandle pfPageHandle;
    rc=pfFileHandle.AllocatePage(pfPageHandle);         /*注意需要手动unpin!*/
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 4.将RM 层的头信息写进文件的第一个page */
    char* pPgData;
    RM_FileHdr rmFileHdr;
    rc=pfPageHandle.GetData(pPgData);         /*获取第一个page数据部分的指针*/
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }
    rmFileHdr.firstFreePage=RM_PAGE_LIST_END;
    rmFileHdr.numPages=0;                     /*本实现中,numPages表示数据页个数,不算文件头*/
    rmFileHdr.recordSize=recordSize;
    memcpy(pPgData,&rmFileHdr,sizeof(rmFileHdr));

    /* 5.获取文件头的页号,以留后用*/
    PageNum rmFileHdrPgNum;
    pfPageHandle.GetPageNum(rmFileHdrPgNum);  /* 返回值最多warning,不用错误处理 */

    /* 6.将文件头标记为dirty,表示有修改*/
    rc=pfFileHandle.MarkDirty(rmFileHdrPgNum);
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 7.unpin文件头,之后可换回磁盘 */
    rc=pfFileHandle.UnpinPage(rmFileHdrPgNum);
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 8.关闭文件 */
    rc=pfManager->CloseFile(pfFileHandle);
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    return OK_RC;
}

// Destroy the file with the given filename => 直接调用PF层销毁文件即可
RC RM_Manager::DestroyFile(const char *fileName) {
    RC rc=pfManager->DestroyFile(fileName);
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }
    return OK_RC;
}

// Open the file with the given filename with the specified filehandle
/* 打开一个文件,将其与RM_FileHandle对象关联*/
RC RM_Manager::OpenFile(const char *fileName, RM_FileHandle &fileHandle) {
    /* 1.调用PF层打开文件*/
    PF_FileHandle pfFileHandle;
    RC rc=pfManager->OpenFile(fileName,pfFileHandle);
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    /**/
    /* 2.利用pfFileHandle中,RM层的头信息,来初始化fileHandle的成员变量*/
    fileHandle.Open(pfFileHandle);

    return OK_RC;
}

// Close the file with the given filehandle => 关闭文件(真正在文件层面的关闭!)
RC RM_Manager::CloseFile(RM_FileHandle &fileHandle) {
    /* 1.获取PF层的PF_FileHandle*/
    PF_FileHandle* pfFileHandle;
    fileHandle.GetPfFileHandle(pfFileHandle);

    /* 2.如果修改了rmFileHdr对象,需要将其写入RM文件头!*/
    if(fileHandle.IsHdrChanged()){      
        PF_PageHandle pageHandle;
        pfFileHandle->GetThisPage(1,pageHandle);       /* RM层的头*/

        RM_FileHdr rmFileHdr;
        fileHandle.GetRmFileHdr(rmFileHdr);

        char* pPageData;
        pageHandle.GetData(pPageData);

        memcpy(pPageData,&rmFileHdr,sizeof(rmFileHdr));

        pfFileHandle->MarkDirty(1);

        pfFileHandle->UnpinPage(1);
    }

    /* 3.关闭文件*/
    pfManager->CloseFile(*pfFileHandle);
       
    return OK_RC;
}





 