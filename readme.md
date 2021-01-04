


# 写在前面
- skeleton code中 //形式的注释保留; 自己添加的注释全部为 /**/格式
- 通过以下项目的commit历史,大概可整理出redbase的skeletonCode:https://github.com/adityabhandari1992/cs346-redbase
- 为了易懂,变量名稍微长了一点; 命名有的地方不统一,视上下文而定

- 错误检查实在太影响代码阅读观感,为了简便,很多地方都没写(全)

# Buffer
## 设计
- **缓冲区的组织**  
分为两个双向链表:free、used
- **LRU与MRU**  


# PF
## 设计
## 要点/问题
- **unlink(fname)函数**  
`unlink`函数使文件引用数减一(这个引用是指硬链接数),当引用数为零时,操作系统就删除文件.但若有进程已经打开文件,则只有最后一个引用(这个引用是动态引用,是打开该文件的进程数!)该文件的文件描述符关闭,该文件才会被删除;  
一个inode在OS有两个引用计数:一个静态的,也就是所谓的硬链接个数,这个值是持久性保持,会反映到硬盘上;另一个是动态的,也就是有多少的个进程在使用此inode,这个是动态的,硬盘上根本没有对应记录.
更多参考:[关于unlink函数](https://www.phpfans.net/ask/MTI3MTI3MQ.html)
更多参考:[Linux下unlink函数的使用](https://blog.csdn.net/judgejames/article/details/83749669?utm_medium=distribute.pc_relevant.none-task-blog-BlogCommendFromMachineLearnPai2-1.control&depth_1-utm_source=distribute.pc_relevant.none-task-blog-BlogCommendFromMachineLearnPai2-1.control)


- **如何给文件分配新的页?**  
见PF_FileHandle->AllocatePage();   
当文件的空闲链表为空时,会自动增加最大页号,然后调用pBufferMgr->AllocatePage(fd,pageNum,pBuf)  
这个pageNum暂时在磁盘上并没有空间,但是缓冲区管理器为其分配了缓冲区,当修改了缓冲区后,会自动将缓冲区
数据写回到磁盘上对应的位置,从而间接给文件增加了页!!!
- **一定注意**  
PF_FileHdr没有算入文件页,从而也不会存入redbase的缓冲区,所以对PF文件头的修改,需要手动写回磁盘,而不是通过Unpin()!!! => 见PF_FileHandle -> FlushPages()

# RM
## RM设计
...暂略.
见rm.h
## 要点/问题
- **什么时候pin什么时候unpin??**  
只需记住一条规则:`如果一个page不能安全地写回磁盘,那么它必须是pinned`,否则可以unpin  
对于GetThisPage,它一定被pin在了缓冲区,需要手动unpin

- **RM层,page的空闲链表是怎么组织的?**  
 4.空闲链表的组织方式:RM_FileHdr类似于头结点,firstFreePage指向第一个数据节点,使用空闲链表时从头开始选取节点;若需要插入节点,头插法!

- **关于RM_FileScan的改进建议**  
OpenScan()函数输入的参数只有一个属性,当出现如下情况时:R.attr1=4 AND R.attr2="icg"; 就需要两次扫描表  
=> 应该考虑针对这种情况优化,因为它其实只需要一次扫描就可以的


# CPP杂七杂八
- **成员函数后面有const修饰**  
const的理解:表示成员函数隐含传入的this指针为const指针  
=> 决定了在该成员函数中,任意修改它所在的类的成员的操作都是不允许的 

- **cpp创建对象的两种方式**  
1.通过new => 需要指针承接,需要手动释放内存;  
2.直接ClassName Obj(param); => 自动垃圾回收!!

- **const_cast**  
https://www.cnblogs.com/ider/archive/2011/07/22/cpp_cast_operator_part2.html


- **warning: non-static data member initializers only available with -std=c++11 or -std=gnu++11**  
在.h头文件中直接对变量赋值,这是c11之后的特性,编译时需要加上-std=c++11选项

- **友元类**  
比如:
```
class PF_FileHandle {
   friend class PF_Manager;                     /*指定友元类,可直接访问私有数据,而不必通过函数提供接口!*/
public:
   PF_FileHandle  ();                            // Default constructor
   ~PF_FileHandle ();                            // Destructor

   // Copy constructor
   PF_FileHandle  (const PF_FileHandle &fileHandle);
    .......
}
```
这样PF_Manager可直接访问PF_FileHandle的私有成员变量,而不需要PF_FileHandle专门提供私有成员的访问接口

# 暂时备注
## RM
- RM_Manager的OpenFile暂时陷入僵局,先写其他类  --12.20
- RM_FileHandle应该还需要另设一个函数来初始化对象(传入PF_FileHandle的引用、记录大小等)...待完成?
- RM_FileHandle中,ForcePages还需要考虑当强制写回的是文件头时,需要先将hdr写回缓冲区!!! => 待完成

## buffer
- GetPage函数尚未完全搞懂
- AllocateBlock中,页号那样计算的理由??