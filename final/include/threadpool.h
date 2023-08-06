/*
1.submit 可变参 提交任务
2.future 代替 result类
*/
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<vector>
#include<thread>
#include <queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>
#include<thread>
#include<future>
#include<iostream>
const int TASK_MAX = 2; //INT32_MAX;
const int THREAD_MAX = 10;
const int THREAD_MAX_IDLE_TIME = 10; //60s空闲 回收线程
//线程池 支持 fixed 和 cached 两种模式
enum PoolMode
{
    MODE_FIXED, // 线程数量固定
    MODE_CACHED, // 线程数量可动态增长
};

//线程类型
class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    //构造函数
    Thread(ThreadFunc func):func_(func)
    ,threadId_(generate_id++)
    {

    }
    //析构函数
    ~Thread() = default;
    // 启动线程
    void start(){
        // 需要执行函数
        std::thread t(this->func_,threadId_);
        t.detach(); //设置分离线程 
    }
    // 获取线程id
    int getID() const{
        return threadId_;
    }
private:
    ThreadFunc func_;
    static int generate_id;
    int threadId_; //线程id 用来映射 删除vector中哪个thread
};
int Thread::generate_id = 0;
//线程池类型
class ThreadPool
{
public:
    //线程池构造函数
    ThreadPool()
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
    //线程池析构函数
    ~ThreadPool()
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
    // 判断是否运行
    bool checkPoolRunning() const{
        return isPoolRunning_; 
    }
    // 设置模式
    void setMode(PoolMode mode){
        if(checkPoolRunning())return;
        _nowMode = mode;
    }
    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency()){
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
    // 设置taskQueue 任务上限
    void setTaskQueMaxSize(int threshhold)
    {
        if(checkPoolRunning())return;
        _maxTaskSize = threshhold;
    }
    // 设置线程上限
    void setThreadMaxSize(int threshhold){
        if(checkPoolRunning())return;
        if(PoolMode::MODE_CACHED == _nowMode)
        {
            _maxThreadSize = threshhold;
        }
    }
    // 提交任务 可变参数模版编程
    // 接受任意函数，任意数量参数
    // 引用折叠
    //Result submit(std::shared_ptr<Task>sp);
    template<typename Func,typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> //推导函数返回值类型 
    {
        // 打包任务，加入任务队列
        using TaskType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<TaskType()>>(
            std::bind(std::forward<Func>(func),std::forward<Args>(args)...) // 完美转发 保持左值 右值特性
        );
        std::future<TaskType> result = task->get_future();
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
            auto task = std::make_shared<std::packaged_task<TaskType()>>([]()-> TaskType {
                return TaskType();
            });
            (*task)();
            return task->get_future();
        }
        // 有空位 提交任务
        _taskQueue.emplace([task](){ (*task)();}); // 队列接收void()函数，这里封装一下，在这个函数内部执行task函数
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
        return result;
    }
    // // 设置初始线程数
    // void setInitThreadSize(int size);
    // 禁止拷贝构造、赋值构造
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    std::unordered_map<int,std::unique_ptr<Thread>>_threads; //线程map
    //std::vector<std::unique_ptr<Thread>> _threads; // 线程列表 智能指针管理内存
    size_t _initThreadSize; // 初始线程数量
    int _maxThreadSize; // 线程最大上限 适用于cached 模式
    std::atomic_int curThreadSize_; //当前总线程数量
    std::atomic_int idleThreadSize_; // 工作线程数量

    // Task任务 就是函数对象
    using Task = std::function<void()>; // 不能确定函数的返回值类型，加一个中间层解决；参数可以通过绑定器传进去
    //std::queue<std::shared_ptr<Task>> _taskQueue; // 任务队列 
    std::queue<Task> _taskQueue;// 任务队列 
    std::atomic_int _taskSize; // 队列任务数量 
    int _maxTaskSize; // 任务最大上限
    std::mutex _taskQueMtx; // 互斥访问任务队列
    std::condition_variable _notFull; // 表示任务队列不满
    std::condition_variable _notEmpty; // 表示任务队列不空
    std::condition_variable _exitCond; // 等待线程资源全部回收

    PoolMode _nowMode; // 当前线程池工作模式
    std::atomic_bool isPoolRunning_; // 线程池运行状态
    
private:
    void threadFunc(int threadID)
    {
        auto lastTime = std::chrono::high_resolution_clock().now();
        // 循环接受任务
        for(;;)
        {
            //std::shared_ptr<Task>task;
            Task task;
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
                //task->exec();
                task();
                // 执行完任务 通知
                std::cout<<"Task Finished"<<std::endl;
            }
            idleThreadSize_++; // 任务完成 空闲
            // 更新使用时间
            lastTime = std::chrono::high_resolution_clock().now();
        }
    }
};

#endif