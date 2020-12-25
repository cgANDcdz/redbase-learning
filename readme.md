


# 写在前面
- skeleton code中 //形式的注释保留; 自己添加的注释全部为 /**/格式
- 通过以下项目的commit历史,大概可整理出redbase的skeletonCode:https://github.com/adityabhandari1992/cs346-redbase
- 为了易懂,变量名稍微长了一点; 命名有的地方不统一,视上下文而定

- 错误检查实在太影响代码阅读观感,为了简便,很多地方都没写(全)

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



# 暂时备注
- RM_Manager的OpenFile暂时陷入僵局,先写其他类  --12.20
- RM_FileHandle应该还需要另设一个函数来初始化对象(传入PF_FileHandle的引用、记录大小等)...待完成?
- RM_FileHandle中,ForcePages还需要考虑当强制写回的是文件头时,需要先将hdr写回缓冲区!!! => 待完成
