#include <iostream>
#include<chrono>
#include "threadpool.h"
using namespace std;
int sum1(int a,int b,int c)
{
    //cout<<a+b<<endl;
    this_thread::sleep_for(chrono::seconds(3));
    return a+b+c;
}
int main(int argc, char *argv[])
{
   ThreadPool pool;
   //pool.setMode(PoolMode::MODE_CACHED);
   pool.start(2);
   auto r1 = pool.submitTask(sum1,10,20,30);
   auto r2 = pool.submitTask(sum1,1,20,30);
   auto r3 = pool.submitTask(sum1,10,2,30);
   auto r4 = pool.submitTask(sum1,10,20,3);
   auto r5 = pool.submitTask(sum1,10,20,3);
   cout<<r1.get()<<endl;
   cout<<r2.get()<<endl;
   cout<<r3.get()<<endl;
   cout<<r4.get()<<endl;
   cout<<r5.get()<<endl;
}
 