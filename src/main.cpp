#include <iostream>
#include<chrono>
#include "threadpool.h"

using namespace std;
using uLong =unsigned long long;
class MyTask : public Task
{
    public:
        MyTask(int b,int e):begin_(b),end_(e){}
        Any run() 
        {
            cout<<this_thread::get_id()<<" *****My task Start*****"<<endl;
            uLong sum=0;
            for(int i=begin_;i<=end_;i++)
                sum+=i;
            this_thread::sleep_for(std::chrono::seconds(3));
            cout<<this_thread::get_id()<<"*****My task End*****"<<endl;
            cout<<"*****Part Result is *****  "<< sum<<endl;
            return sum;
        }
    private:
        int begin_;
        int end_;
};
int main(int argc, char *argv[])
{
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(2);
        
        Result rs1 = pool.submit(make_shared<MyTask>(1,10000000));
        Result rs2 = pool.submit(make_shared<MyTask>(10000001,20000000));
        auto rs3 = pool.submit(make_shared<MyTask>(10000001,20000000));
        auto rs4 = pool.submit(make_shared<MyTask>(10000001,20000000));
        pool.submit(make_shared<MyTask>(10000001,20000000));
        pool.submit(make_shared<MyTask>(10000001,20000000));
        // uLong s1=rs1.get().cast_<uLong>();
        // uLong s2=rs2.get().cast_<uLong>();
        // uLong s3=rs3.get().cast_<uLong>();
        // uLong s4=rs4.get().cast_<uLong>();
        // uLong s2=rs2.get().cast_<uLong>();
        // cout<<s1;
    }
    
    cout<<"main over"<<endl;
    getchar();
#if 0
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(4);
        Result rs1 = pool.submit(make_shared<MyTask>(1,10000000));
        Result rs2 = pool.submit(make_shared<MyTask>(10000001,20000000));
        Result rs3 = pool.submit(make_shared<MyTask>(20000001,30000000));
        Result rs4 = pool.submit(make_shared<MyTask>(1,10000000));
        Result rs5 = pool.submit(make_shared<MyTask>(10000001,20000000));
        Result rs6 = pool.submit(make_shared<MyTask>(20000001,30000000));

        uLong s1=rs1.get().cast_<uLong>();
        uLong s2=rs2.get().cast_<uLong>();
        uLong s3=rs3.get().cast_<uLong>();
        uLong sum = s1+s2+s3;
        cout<<sum<<endl;
    }
    getchar();
    //std::this_thread::sleep_for(std::chrono::seconds(3));
#endif
}
