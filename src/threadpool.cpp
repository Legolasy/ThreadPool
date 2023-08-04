#include "threadpool.h"
#include<functional>
#include<iostream>
#include<chrono>
const int TASK_MAX = INT32_MAX;
const int THREAD_MAX = 10;
const int THREAD_MAX_IDLE_TIME = 10; //60s空闲 回收线程
//构造函数
ThreadPool::ThreadPool()
:_initThreadSize(4)
,_taskSize(0)
,idleThreadSize_(0)
,_maxTaskSize(TASK_MAX)
,_maxThreadSize(THREAD_MAX)
,_nowMode(PoolMode::MODE_FIXED)
,isPoolRunning_(false)
,curThreadSize_(0)
{
    
}
// 析构函数
ThreadPool::~ThreadPool() 
{
    isPoolRunning_ = false;
    // 等待线程池中的线程 全部返回才析构
    std::unique_lock<std::mutex>lock(_taskQueMtx);
    // 先抢锁 在notify
    _notEmpty.notify_all();
    _exitCond.wait(lock,[&]() -> bool {
        return _threads.size() == 0;
    });
}
bool ThreadPool::checkPoolRunning() const
{ 
    return isPoolRunning_; 
}
// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode) {
    if(checkPoolRunning())return;
    _nowMode = mode;
}
void ThreadPool::start(int initThreadSize) 
{
    isPoolRunning_ = true;
    _initThreadSize = initThreadSize;
    curThreadSize_ = initThreadSize;
    // 创建线程对象
    for(size_t i=0;i<_initThreadSize;i++)
    {
        // 绑定器 决定线程执行的函数
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
        int tid = ptr->getID();
        _threads.emplace(tid,std::move(ptr));
        //_threads.emplace_back(std::move(ptr));
        //_threads.emplace_back(new Thread(std::bind(&ThreadPool::threadFunc,this)));
    }

    // 启动线程 std::vector<Thread*> _threads
    for(size_t i=0;i<_initThreadSize;i++)
    {
        // 线程执行 需要task 函数
        _threads[i]->start();
        idleThreadSize_++; // 空闲数量
    } 
}
// 设置任务队列上限
void ThreadPool::setTaskQueMaxSize(int threshhold) 
{
    if(checkPoolRunning())return;
    _maxTaskSize = threshhold;
}
// 设置线程数量上限
void ThreadPool::setThreadMaxSize(int threshhold)
{
    if(checkPoolRunning())return;
    if(PoolMode::MODE_CACHED == _nowMode)
    {
        _maxThreadSize = threshhold;
    }
}
Result ThreadPool::submit(std::shared_ptr<Task> sp) {
    // 上锁 
    std::unique_lock<std::mutex>lock(_taskQueMtx);
    // 用户提交任务 阻塞超过一秒 判断提交失败
    // 等待任务 线程通信 cv 
    if( !_notFull.wait_for(lock,std::chrono::seconds(1),[&]()->bool {
        return _taskQueue.size() < (size_t)_maxTaskSize;
    }))
    {
        // 超时 出错
        std::cerr << "Task Submit TimeOut 1s , TaskQue is Full" << std::endl;
        return Result(sp,false);
    }
    // while(_taskQueue.size() == (size_t)_maxTaskSize)
    // {
    //     //没空位
    //     if(std::cv_status::no_timeout == _notFull.wait_for(lock,std::chrono::seconds(1))) // 让出锁 进入等待状态
    //     {
    //         // 超时 出错
    //         std::cerr << "Task Submit TimeOut 1s , TaskQue is Full" << std::endl;
    //         return;
    //     }
    // }
    // _notFull.wait(lock,[&]()->bool {
    //     return _taskQueue.size() < _maxTaskSize;
    // });

    // 有空位 提交任务
    _taskQueue.emplace(sp);
    _taskSize++;

    // notEmpty 通知 可以分配工作线程
    _notEmpty.notify_all();

    // cached 模式下，根据任务数量和空闲线程数量，判断是否要 新增线程
    // 适合 小而快的任务
    // 线程太多对性能有影响
    //cached模式 + 当前线程数量小于线程数量上限 + 当前任务数量大于空闲线程数量
    if(PoolMode::MODE_CACHED == _nowMode 
        && curThreadSize_ < _maxThreadSize 
        && _taskSize > idleThreadSize_)
    {
        //创建新线程
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
        int tid = ptr->getID();
        _threads.emplace(tid,std::move(ptr));
        _threads[tid]->start(); // 启动线程
        curThreadSize_++;
        idleThreadSize_++;
        std::cout<<"Create New Thread:"<<std::endl;
    }
    return Result(sp);
}
// 线程执行任务函数 线程从任务队列消费任务
// 线程池里有任务，必须等到任务完成，才能析构
void ThreadPool::threadFunc(int threadID) 
{
    auto lastTime = std::chrono::high_resolution_clock().now();
    // 循环接受任务
    for(;;)
    {
        std::shared_ptr<Task>task;
        {
            // 1.获取锁
            std::unique_lock<std::mutex>lock(_taskQueMtx);
            
            std::cout<<"tid:"<<std::this_thread::get_id()<<" Try Get Task"<<std::endl;

            // TODO: cached模式下 空闲线程 超60s 无任务，销毁
            // 区分超时返回 and 有任务待执行返回
            while (_taskQueue.size() == 0)
            {
                // 没任务 当前如果线程池已经关闭，则回收线程
                if(isPoolRunning_ == false)
                {
                    //回收线程
                    _threads.erase(threadID);
                    _exitCond.notify_all();
                    std::cout<<"ThreadID:"<<std::this_thread::get_id()<<" exit!"<<std::endl;
                    return;
                }
                // cached 模式 如果空闲时间达到THREAD_MAX_IDLE_TIME 回收线程
                if(PoolMode::MODE_CACHED == _nowMode)
                {
                    // 条件变量超时返回 没抢到任务
                    if(std::cv_status::timeout ==
                        _notEmpty.wait_for(lock,std::chrono::seconds(1)))
                    {
                        auto nowTime = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime-lastTime);
                        if(curThreadSize_>_initThreadSize && dur.count() >= THREAD_MAX_IDLE_TIME)
                        {
                            // 回收当前线程
                            // 线程vector 删除对象；线程相关变量修改
                            _threads.erase(threadID);
                            _exitCond.notify_all();
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout<<"ThreadID:"<<std::this_thread::get_id()<<" exit!"<<std::endl;
                            return; // 结束线程
                        }
                    }
                }
                else // fixed 模式 一直wait
                {
                    //_notEmpty.wait(lock,[&]()->bool {return _taskQueue.size() > 0;});
                    _notEmpty.wait(lock);
                }
                // 如果此时_notEmpty被唤醒，检查是不是Pool要被析构了
                // if(isPoolRunning_ == false)
                // {
                //     // 回收线程
                //     _threads.erase(threadID);
                //     _exitCond.notify_all();
                //     std::cout<<"ThreadID:"<<std::this_thread::get_id()<<" exit!"<<std::endl;
                //     return;
                // }
            }
            // 如果此时_notEmpty被唤醒，检查是不是Pool要被析构了
            // 2.等待 notEmpty 信号
            // while(_taskQueue.size() == 0)
            // {
            //     _notEmpty.wait(lock);
            // }
            
            std::cout<<"tid:"<<std::this_thread::get_id()<<" Success Get Task"<<std::endl;
            idleThreadSize_--; // 线程取任务 不空闲
            // 3.任务队列获取任务
            task = _taskQueue.front();
            _taskQueue.pop();
            _taskSize--;
            
            // 可以继续执行任务
            if(_taskQueue.size() > 0)
                _notEmpty.notify_all();

            // 可以继续提交任务
            _notFull.notify_all();
            // 4.当前线程执行任务 释放锁
        }
        if(task!=nullptr)
        {
            task->exec();
            // 执行完任务 通知
            std::cout<<"Task Finished"<<std::endl;
        }
        idleThreadSize_++; // 任务完成 空闲
        // 更新使用时间
        lastTime = std::chrono::high_resolution_clock().now();
    }
}

Thread::Thread(ThreadFunc func) 
:func_(func)
,threadId_(generate_id++)
{

}

Thread::~Thread() 
{

}

int Thread::generate_id = 0;
// 启动线程
void Thread::start() 
{
    // 需要执行函数
    std::thread t(this->func_,threadId_);
    t.detach(); //设置分离线程 
}

int Thread::getID() const
{
    return threadId_;
}

Any Result::get() {  // 用户调用
    if(isValid_ == false)
    {
        return ""; //不合法的 返回一个空
    }
    sem_.wait(); // 任务没完成 阻塞 （信号量没资源生成）
    return std::move(data_);
}

void Result::setVal(Any any) // task调用
{
    //存储task 返回值
    data_ = std::move(any);
    sem_.post(); //任务返回值获取了，生成一个资源，让用户get不阻塞
}

//封装运行
void Task::exec()
{
    if(rs_ != nullptr)
        rs_->setVal(run()); //多态调用
}

void Task::setResult(Result* rs)
{
    rs_ = rs;
}
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task_->setResult(this);
}