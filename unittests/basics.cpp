#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <taskflow/taskflow.hpp>
#include <vector>
#include <utility>
#include <chrono>
#include <limits.h>

// --------------------------------------------------------
// Testcase: Type
// --------------------------------------------------------
TEST_CASE("Type" * doctest::timeout(300)) {

  tf::Taskflow taskflow, taskflow2;

  auto t1 = taskflow.emplace([](){});
  auto t2 = taskflow.emplace([](){ return 1; });
  auto t3 = taskflow.emplace([](tf::Subflow&){ });
  auto t4 = taskflow.composed_of(taskflow2);

  REQUIRE(t1.type() == tf::TaskType::STATIC);
  REQUIRE(t2.type() == tf::TaskType::CONDITION);
  REQUIRE(t3.type() == tf::TaskType::DYNAMIC);
  REQUIRE(t4.type() == tf::TaskType::MODULE);
}

// --------------------------------------------------------
// Testcase: Builder
// --------------------------------------------------------
TEST_CASE("Builder" * doctest::timeout(300)) {

  const size_t num_tasks = 100;

  tf::Taskflow taskflow;
  tf::Executor executor;

  std::atomic<int> counter {0};
  std::vector<tf::Task> silent_tasks;
  std::vector<tf::Task> tasks;

  SUBCASE("EmptyFlow") {
    for(unsigned W=1; W<32; ++W) {
      tf::Executor executor(W);
      tf::Taskflow taskflow;
      REQUIRE(taskflow.num_tasks() == 0);
      REQUIRE(taskflow.empty() == true);
      executor.run(taskflow).wait();
    }
  }
    
  SUBCASE("Placeholder") {
    
    for(size_t i=0; i<num_tasks; ++i) {
      silent_tasks.emplace_back(taskflow.placeholder().name(std::to_string(i)));
    }

    for(size_t i=0; i<num_tasks; ++i) {
      REQUIRE(silent_tasks[i].name() == std::to_string(i));
      REQUIRE(silent_tasks[i].num_dependents() == 0);
      REQUIRE(silent_tasks[i].num_successors() == 0);
    }

    for(auto& task : silent_tasks) {
      task.work([&counter](){ counter++; });
    }

    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks);
  }

  SUBCASE("EmbarrassinglyParallel"){

    for(size_t i=0;i<num_tasks;i++) {
      tasks.emplace_back(taskflow.emplace([&counter]() {counter += 1;}));
    }

    REQUIRE(taskflow.num_tasks() == num_tasks);
    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks);
    REQUIRE(taskflow.num_tasks() == 100);

    counter = 0;
    
    for(size_t i=0;i<num_tasks;i++){
      silent_tasks.emplace_back(taskflow.emplace([&counter]() {counter += 1;}));
    }

    REQUIRE(taskflow.num_tasks() == num_tasks * 2);
    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks * 2);
    REQUIRE(taskflow.num_tasks() == 200);
  }
  
  SUBCASE("BinarySequence"){
    for(size_t i=0;i<num_tasks;i++){
      if(i%2 == 0){
        tasks.emplace_back(
          taskflow.emplace([&counter]() { REQUIRE(counter == 0); counter += 1;})
        );
      }
      else{
        tasks.emplace_back(
          taskflow.emplace([&counter]() { REQUIRE(counter == 1); counter -= 1;})
        );
      }
      if(i>0){
        //tasks[i-1].first.precede(tasks[i].first);
        tasks[i-1].precede(tasks[i]);
      }

      if(i==0) {
        //REQUIRE(tasks[i].first.num_dependents() == 0);
        REQUIRE(tasks[i].num_dependents() == 0);
      }
      else {
        //REQUIRE(tasks[i].first.num_dependents() == 1);
        REQUIRE(tasks[i].num_dependents() == 1);
      }
    }
    executor.run(taskflow).get();
  }

  SUBCASE("LinearCounter"){
    for(size_t i=0;i<num_tasks;i++){
      tasks.emplace_back(
        taskflow.emplace([&counter, i]() { 
          REQUIRE(counter == i); counter += 1;}
        )
      );
      if(i>0) {
        tasks[i-1].precede(tasks[i]);
      }
    }
    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks);
    REQUIRE(taskflow.num_tasks() == num_tasks);
  }
 
  SUBCASE("Broadcast"){
    auto src = taskflow.emplace([&counter]() {counter -= 1;});
    for(size_t i=1; i<num_tasks; i++){
      silent_tasks.emplace_back(
        taskflow.emplace([&counter]() {REQUIRE(counter == -1);})
      );
      src.precede(silent_tasks.back());
    }
    executor.run(taskflow).get();
    REQUIRE(counter == - 1);
    REQUIRE(taskflow.num_tasks() == num_tasks);
  }

  SUBCASE("Succeed"){
    auto dst = taskflow.emplace([&]() { REQUIRE(counter == num_tasks - 1);});
    for(size_t i=1;i<num_tasks;i++){
      silent_tasks.emplace_back(
        taskflow.emplace([&counter]() {counter += 1;})
      );
      dst.succeed(silent_tasks.back());
    }
    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks - 1);
    REQUIRE(taskflow.num_tasks() == num_tasks);
  }

  SUBCASE("MapReduce"){

    auto src = taskflow.emplace([&counter]() {counter = 0;});
    
    auto dst = taskflow.emplace(
      [&counter, num_tasks]() { REQUIRE(counter == num_tasks);}
    );

    for(size_t i=0;i<num_tasks;i++){
      silent_tasks.emplace_back(
        taskflow.emplace([&counter]() {counter += 1;})
      );
      src.precede(silent_tasks.back());
      dst.succeed(silent_tasks.back());
    }
    executor.run(taskflow).get();
    REQUIRE(taskflow.num_tasks() == num_tasks + 2);
  }

  SUBCASE("Linearize"){
    for(size_t i=0;i<num_tasks;i++){
      silent_tasks.emplace_back(
        taskflow.emplace([&counter, i]() { 
          REQUIRE(counter == i); counter += 1;}
        )
      );
    }
    taskflow.linearize(silent_tasks);
    executor.run(taskflow).get();
    REQUIRE(counter == num_tasks);
    REQUIRE(taskflow.num_tasks() == num_tasks);
  }

  SUBCASE("Kite"){
    auto src = taskflow.emplace([&counter]() {counter = 0;});
    for(size_t i=0;i<num_tasks;i++){
      silent_tasks.emplace_back(
        taskflow.emplace([&counter, i]() { 
          REQUIRE(counter == i); counter += 1; }
        )
      );
      src.precede(silent_tasks.back());
    }
    taskflow.linearize(silent_tasks);
    auto dst = taskflow.emplace(
      [&counter, num_tasks]() { REQUIRE(counter == num_tasks);}
    );

    for(auto task : silent_tasks) dst.succeed(task);
    executor.run(taskflow).get();
    REQUIRE(taskflow.num_tasks() == num_tasks + 2);
  }
}

// --------------------------------------------------------
// Testcase: Creation
// --------------------------------------------------------
TEST_CASE("Creation" * doctest::timeout(300)) {

  std::vector<int> dummy(1000, -1);

  auto create_taskflow = [&] () {
    for(int i=0; i<10; ++i) {
      tf::Taskflow tf;
      tf.for_each(dummy.begin(), dummy.end(), [] (int) {});
    }
  };

  SUBCASE("One") {
    create_taskflow();
    REQUIRE(dummy.size() == 1000);
    for(auto item : dummy) {
      REQUIRE(item == -1);
    }
  }

  SUBCASE("Two") {
    std::thread t1(create_taskflow);
    std::thread t2(create_taskflow);
    t1.join();
    t2.join();
    REQUIRE(dummy.size() == 1000);
    for(auto item : dummy) {
      REQUIRE(item == -1);
    }
  }
  
  SUBCASE("Four") {
    std::thread t1(create_taskflow); 
    std::thread t2(create_taskflow); 
    std::thread t3(create_taskflow); 
    std::thread t4(create_taskflow); 
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    REQUIRE(dummy.size() == 1000);
    for(auto item : dummy) {
      REQUIRE(item == -1);
    }
  }

}

// --------------------------------------------------------
// Testcase: STDFunction
// --------------------------------------------------------
TEST_CASE("STDFunction" * doctest::timeout(300)) {

  tf::Taskflow taskflow;
  tf::Executor executor;

  int counter = 0;

  std::function<void()> func1  = [&] () { ++counter; };
  std::function<int()>  func2  = [&] () { ++counter; return 0; };
  std::function<void()> func3  = [&] () { };
  std::function<void()> func4  = [&] () { ++counter;};
  
  // scenario 1
  auto A = taskflow.emplace(func1);
  auto B = taskflow.emplace(func2);
  auto C = taskflow.emplace(func3);
  auto D = taskflow.emplace(func4);
  A.precede(B);
  B.precede(C, D);
  executor.run(taskflow).wait();
  REQUIRE(counter == 2);

  return;
  
  // scenario 2
  counter = 0;
  A.work(func1);
  B.work(func2);
  C.work(func4);
  D.work(func3);
  executor.run(taskflow).wait();
  REQUIRE(counter == 3);
  
  // scenario 3
  taskflow.clear();
  std::tie(A, B, C, D) = taskflow.emplace(
    std::move(func1), std::move(func2), std::move(func3), std::move(func4)
  );
  A.precede(B);
  B.precede(C, D);
  counter = 0;
  executor.run(taskflow).wait();
  REQUIRE(counter == 2);
}

// --------------------------------------------------------
// Testcase: Iterators
// --------------------------------------------------------
TEST_CASE("Iterators" * doctest::timeout(300)) {

  SUBCASE("Order") {
    tf::Taskflow taskflow;

    auto A = taskflow.emplace([](){}).name("A");
    auto B = taskflow.emplace([](){}).name("B");
    auto C = taskflow.emplace([](){}).name("C");
    auto D = taskflow.emplace([](){}).name("D");
    auto E = taskflow.emplace([](){}).name("E");

    A.precede(B, C, D, E);
    E.succeed(B, C, D);

    A.for_each_successor([&, i=0] (tf::Task s) mutable {
      switch(i++) {
        case 0:
          REQUIRE(s == B);
        break;
        case 1:
          REQUIRE(s == C);
        break;
        case 2:
          REQUIRE(s == D);
        break;
        case 3:
          REQUIRE(s == E);
        break;
        default:
        break;
      }
    });

    E.for_each_dependent([&, i=0](tf::Task s) mutable {
      switch(i++) {
        case 0:
          REQUIRE(s == A);
        break;
        case 1:
          REQUIRE(s == B);
        break;
        case 2:
          REQUIRE(s == C);
        break;
        case 3:
          REQUIRE(s == D);
        break;
      }
    });
  }
  
  SUBCASE("Generic") {
    tf::Taskflow taskflow;

    auto A = taskflow.emplace([](){}).name("A");
    auto B = taskflow.emplace([](){}).name("B");
    auto C = taskflow.emplace([](){}).name("C");
    auto D = taskflow.emplace([](){}).name("D");
    auto E = taskflow.emplace([](){}).name("E");

    std::vector<tf::Task> tasks;

    taskflow.for_each_task([&tasks](tf::Task s){
      tasks.push_back(s);
    });

    REQUIRE(std::find(tasks.begin(), tasks.end(), A) != tasks.end());
 
    A.precede(B);

    A.for_each_successor([B](tf::Task s){ REQUIRE(s==B); });
    B.for_each_dependent([A](tf::Task s){ REQUIRE(s==A); });

    A.precede(C);
    A.precede(D);
    A.precede(E);
    C.precede(B);
    D.precede(B);
    E.precede(B);
    
    int counter{0}, a{0}, b{0}, c{0}, d{0}, e{0};
    A.for_each_successor([&](tf::Task s) {
      counter++;
      if(s == A) ++a;
      if(s == B) ++b;
      if(s == C) ++c;
      if(s == D) ++d;
      if(s == E) ++e;
    });
    REQUIRE(counter == A.num_successors());
    REQUIRE(a==0);
    REQUIRE(b==1);
    REQUIRE(c==1);
    REQUIRE(d==1);
    REQUIRE(e==1);
    
    counter = a = b = c = d = e = 0;
    B.for_each_dependent([&](tf::Task s) {
      counter++;
      if(s == A) ++a;
      if(s == B) ++b;
      if(s == C) ++c;
      if(s == D) ++d;
      if(s == E) ++e;
    });

    REQUIRE(counter == B.num_dependents());
    REQUIRE(a == 1);
    REQUIRE(b == 0);
    REQUIRE(c == 1);
    REQUIRE(d == 1);
    REQUIRE(e == 1);

    A.for_each_successor([](tf::Task s){
      s.name("A");
    });

    REQUIRE(A.name() == "A");
    REQUIRE(B.name() == "A");
    REQUIRE(C.name() == "A");
    REQUIRE(D.name() == "A");
    REQUIRE(E.name() == "A");

    B.for_each_dependent([](tf::Task s){
      s.name("B");
    });
    
    REQUIRE(A.name() == "B");
    REQUIRE(B.name() == "A");
    REQUIRE(C.name() == "B");
    REQUIRE(D.name() == "B");
    REQUIRE(E.name() == "B");

  }
}

// --------------------------------------------------------
// Testcase: Hash
// --------------------------------------------------------
TEST_CASE("Hash" * doctest::timeout(300)) {

  std::hash<tf::Task> hash;
  
  // empty hash
  tf::Task t1, t2;

  REQUIRE(hash(t1) == hash(t2));

  tf::Taskflow taskflow;

  t1 = taskflow.emplace([](){});

  REQUIRE(((hash(t1) != hash(t2)) || (hash(t1) == hash(t2) && t1 != t2)));

  t2 = taskflow.emplace([](){});

  REQUIRE(((hash(t1) != hash(t2)) || (hash(t1) == hash(t2) && t1 != t2)));

  t2 = t1;

  REQUIRE(hash(t1) == hash(t2));
}

// --------------------------------------------------------
// Testcase: SequentialRuns
// --------------------------------------------------------
void sequential_runs(unsigned W) {

  using namespace std::chrono_literals;
  
  size_t num_tasks = 100;
  
  tf::Executor executor(W);
  tf::Taskflow taskflow;

  std::atomic<int> counter {0};
  std::vector<tf::Task> silent_tasks;
    
  for(size_t i=0;i<num_tasks;i++){
    silent_tasks.emplace_back(
      taskflow.emplace([&counter]() {counter += 1;})
    );
  }
  
  SUBCASE("RunOnce"){
    auto fu = executor.run(taskflow);
    REQUIRE(taskflow.num_tasks() == num_tasks);
    fu.get();
    REQUIRE(counter == num_tasks);
  }

  SUBCASE("WaitForAll") {
    executor.run(taskflow);
    executor.wait_for_all();
    REQUIRE(counter == num_tasks); 
  }
  
  SUBCASE("RunWithFuture") {

    std::atomic<size_t> count {0};
    tf::Taskflow f;
    auto A = f.emplace([&](){ count ++; });
    auto B = f.emplace([&](tf::Subflow& subflow){ 
      count ++; 
      auto B1 = subflow.emplace([&](){ count++; });
      auto B2 = subflow.emplace([&](){ count++; });
      auto B3 = subflow.emplace([&](){ count++; });
      B1.precede(B3); B2.precede(B3);
    });
    auto C = f.emplace([&](){ count ++; });
    auto D = f.emplace([&](){ count ++; });

    A.precede(B, C);
    B.precede(D); 
    C.precede(D);

    std::list<tf::Future<void>> fu_list;
    for(size_t i=0; i<500; i++) {
      if(i == 499) {
        executor.run(f).get();   // Synchronize the first 500 runs
        executor.run_n(f, 500);  // Run 500 times more
      }
      else if(i % 2) {
        fu_list.push_back(executor.run(f));
      }
      else {
        fu_list.push_back(executor.run(f, [&, i=i](){ 
          REQUIRE(count == (i+1)*7); })
        );
      }
    }
    
    executor.wait_for_all();

    for(auto& fu: fu_list) {
      REQUIRE(fu.valid());
      REQUIRE(fu.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    }

    REQUIRE(count == 7000);
    
  }

  SUBCASE("RunWithChange") {
    std::atomic<size_t> count {0};
    tf::Taskflow f;
    auto A = f.emplace([&](){ count ++; });
    auto B = f.emplace([&](tf::Subflow& subflow){ 
      count ++; 
      auto B1 = subflow.emplace([&](){ count++; });
      auto B2 = subflow.emplace([&](){ count++; });
      auto B3 = subflow.emplace([&](){ count++; });
      B1.precede(B3); B2.precede(B3);
    });
    auto C = f.emplace([&](){ count ++; });
    auto D = f.emplace([&](){ count ++; });

    A.precede(B, C);
    B.precede(D); 
    C.precede(D);

    executor.run_n(f, 10).get();
    REQUIRE(count == 70);    

    auto E = f.emplace([](){});
    D.precede(E);
    executor.run_n(f, 10).get();
    REQUIRE(count == 140);    

    auto F = f.emplace([](){});
    E.precede(F);
    executor.run_n(f, 10);
    executor.wait_for_all();
    REQUIRE(count == 210);    
    
  }

  SUBCASE("RunWithPred") {
    std::atomic<size_t> count {0};
    tf::Taskflow f;
    auto A = f.emplace([&](){ count ++; });
    auto B = f.emplace([&](tf::Subflow& subflow){ 
      count ++; 
      auto B1 = subflow.emplace([&](){ count++; });
      auto B2 = subflow.emplace([&](){ count++; });
      auto B3 = subflow.emplace([&](){ count++; });
      B1.precede(B3); B2.precede(B3);
    });
    auto C = f.emplace([&](){ count ++; });
    auto D = f.emplace([&](){ count ++; });

    A.precede(B, C);
    B.precede(D); 
    C.precede(D);

    executor.run_until(f, [run=10]() mutable { return run-- == 0; }, 
      [&](){
        REQUIRE(count == 70);
        count = 0;
      }
    ).get();


    executor.run_until(f, [run=10]() mutable { return run-- == 0; }, 
      [&](){
        REQUIRE(count == 70);
        count = 0;
    });

    executor.run_until(f, [run=10]() mutable { return run-- == 0; }, 
      [&](){
        REQUIRE(count == 70);
        count = 0;
      }
    ).get();
    
  }

  SUBCASE("MultipleRuns") {
    std::atomic<size_t> counter(0);

    tf::Taskflow tf1, tf2, tf3, tf4;

    for(size_t n=0; n<16; ++n) {
      tf1.emplace([&](){counter.fetch_add(1, std::memory_order_relaxed);});
    }
    
    for(size_t n=0; n<1024; ++n) {
      tf2.emplace([&](){counter.fetch_add(1, std::memory_order_relaxed);});
    }
    
    for(size_t n=0; n<32; ++n) {
      tf3.emplace([&](){counter.fetch_add(1, std::memory_order_relaxed);});
    }
    
    for(size_t n=0; n<128; ++n) {
      tf4.emplace([&](){counter.fetch_add(1, std::memory_order_relaxed);});
    }
    
    for(int i=0; i<200; ++i) {
      executor.run(tf1);
      executor.run(tf2);
      executor.run(tf3);
      executor.run(tf4);
    }
    executor.wait_for_all();
    REQUIRE(counter == 240000);
    
  }
}


TEST_CASE("SerialRuns.1thread" * doctest::timeout(300)) {
  sequential_runs(1);
}

TEST_CASE("SerialRuns.2threads" * doctest::timeout(300)) {
  sequential_runs(2);
}

TEST_CASE("SerialRuns.3threads" * doctest::timeout(300)) {
  sequential_runs(3);
}

TEST_CASE("SerialRuns.4threads" * doctest::timeout(300)) {
  sequential_runs(4);
}

TEST_CASE("SerialRuns.5threads" * doctest::timeout(300)) {
  sequential_runs(5);
}

TEST_CASE("SerialRuns.6threads" * doctest::timeout(300)) {
  sequential_runs(6);
}

TEST_CASE("SerialRuns.7threads" * doctest::timeout(300)) {
  sequential_runs(7);
}

TEST_CASE("SerialRuns.8threads" * doctest::timeout(300)) {
  sequential_runs(8);
}

// --------------------------------------------------------
// Testcase: WorkerID
// --------------------------------------------------------
void worker_id(unsigned w) {

  tf::Taskflow taskflow;
  tf::Executor executor(w);

  for(int i=0; i<1000; i++) {
    auto A = taskflow.emplace([&](){
      auto id = executor.this_worker_id();
      REQUIRE(id>=0);
      REQUIRE(id< w);
    });

    auto B = taskflow.emplace([&](tf::Subflow& sf){
      auto id = executor.this_worker_id();
      REQUIRE(id>=0);
      REQUIRE(id< w);
      sf.emplace([&](){
        auto id = executor.this_worker_id();
        REQUIRE(id>=0);
        REQUIRE(id< w);
      });
      sf.emplace([&](tf::Subflow&){
        auto id = executor.this_worker_id();
        REQUIRE(id>=0);
        REQUIRE(id< w);
      });
    });

    A.precede(B);
  }

  executor.run_n(taskflow, 100).wait();
}

TEST_CASE("WorkerID.1thread") {
  worker_id(1);
}

TEST_CASE("WorkerID.2threads") {
  worker_id(2);
}

TEST_CASE("WorkerID.3threads") {
  worker_id(3);
}

TEST_CASE("WorkerID.4threads") {
  worker_id(4);
}

TEST_CASE("WorkerID.5threads") {
  worker_id(5);
}

TEST_CASE("WorkerID.6threads") {
  worker_id(6);
}

TEST_CASE("WorkerID.7threads") {
  worker_id(7);
}

TEST_CASE("WorkerID.8threads") {
  worker_id(8);
}

// --------------------------------------------------------
// Testcase: ParallelRuns
// --------------------------------------------------------
void parallel_runs(unsigned w) {

  std::atomic<int> counter;
  std::vector<std::thread> threads;

  auto make_taskflow = [&] (tf::Taskflow& tf) {
    for(int i=0; i<1024; i++) {
      auto A = tf.emplace([&] () { 
        counter.fetch_add(1, std::memory_order_relaxed); 
      });
      auto B = tf.emplace([&] () {
        counter.fetch_add(1, std::memory_order_relaxed); 
      });
      A.precede(B);
    }
  };

  SUBCASE("RunAndWait") {
    tf::Executor executor(w);
    counter = 0;
    for(int t=0; t<32; t++) {
      threads.emplace_back([&] () {
        tf::Taskflow taskflow;
        make_taskflow(taskflow);
        executor.run(taskflow).wait();
      });
    }

    for(auto& t : threads) {
      t.join();
    }
    threads.clear();

    REQUIRE(counter.load() == 32*1024*2);
    
  }
  
  SUBCASE("RunAndWaitForAll") { 
    tf::Executor executor(w);
    counter = 0;
    std::vector<std::unique_ptr<tf::Taskflow>> taskflows(32);
    std::atomic<size_t> barrier(0);
    for(int t=0; t<32; t++) {
      threads.emplace_back([&, t=t] () {
        taskflows[t] = std::make_unique<tf::Taskflow>();
        make_taskflow(*taskflows[t]);
        executor.run(*taskflows[t]);
        ++barrier;    // make sure all runs are issued
      });
    }
    
    while(barrier != 32);
    executor.wait_for_all();
    REQUIRE(counter.load() == 32*1024*2);
    
    for(auto& t : threads) {
      t.join();
    }
    threads.clear();
  }
}


TEST_CASE("ParallelRuns.1thread" * doctest::timeout(300)) {
  parallel_runs(1);
}

TEST_CASE("ParallelRuns.2threads" * doctest::timeout(300)) {
  parallel_runs(2);
}

TEST_CASE("ParallelRuns.3threads" * doctest::timeout(300)) {
  parallel_runs(3);
}

TEST_CASE("ParallelRuns.4threads" * doctest::timeout(300)) {
  parallel_runs(4);
}

TEST_CASE("ParallelRuns.5threads" * doctest::timeout(300)) {
  parallel_runs(5);
}

TEST_CASE("ParallelRuns.6threads" * doctest::timeout(300)) {
  parallel_runs(6);
}

TEST_CASE("ParallelRuns.7threads" * doctest::timeout(300)) {
  parallel_runs(7);
}

TEST_CASE("ParallelRuns.8threads" * doctest::timeout(300)) {
  parallel_runs(8);
}

// --------------------------------------------------------
// Testcase: NestedRuns
// --------------------------------------------------------
void nested_runs(unsigned w) {

  int counter {0};

  struct A {

    tf::Executor executor;
    tf::Taskflow taskflow;

    int& counter;

    A(unsigned w, int& c) : executor{w}, counter{c} { }
  
    void run()
    {
      taskflow.clear();
      auto A1 = taskflow.emplace([&]() { counter++; });
      auto A2 = taskflow.emplace([&]() { counter++; });
      A1.precede(A2);
      executor.run_n(taskflow, 10).wait();
    }

  };
  
  struct B {

    tf::Taskflow taskflow;
    tf::Executor executor;

    int& counter;

    A a_sim;

    B(unsigned w, int& c) : executor{w}, counter{c}, a_sim{w, c} { }
  
    void run()
    {
      taskflow.clear();
      auto B1 = taskflow.emplace([&] () { ++counter; });
      auto B2 = taskflow.emplace([&] () { ++counter; a_sim.run(); });
      B1.precede(B2);
      executor.run_n(taskflow, 100).wait();
    }
  };
  
  struct C {

    tf::Taskflow taskflow;
    tf::Executor executor;

    int& counter;

    B b_sim;

    C(unsigned w, int& c) : executor{w}, counter{c}, b_sim{w, c} { }
  
    void run()
    {
      taskflow.clear();
      auto C1 = taskflow.emplace([&] () { ++counter; });
      auto C2 = taskflow.emplace([&] () { ++counter; b_sim.run(); });
      C1.precede(C2);
      executor.run_n(taskflow, 100).wait();
    }
  };

  C c(w, counter);
  c.run();

  REQUIRE(counter == 220200);
}

TEST_CASE("NestedRuns.1thread") {
  nested_runs(1);
}

TEST_CASE("NestedRuns.2threads") {
  nested_runs(2);
}

TEST_CASE("NestedRuns.3threads") {
  nested_runs(3);
}

TEST_CASE("NestedRuns.4threads") {
  nested_runs(4);
}

TEST_CASE("NestedRuns.8threads") {
  nested_runs(8);
}

TEST_CASE("NestedRuns.16threads") {
  nested_runs(16);
}

// --------------------------------------------------------
// Testcase: ParallelFor
// --------------------------------------------------------

void for_each(unsigned W) {
  
  using namespace std::chrono_literals;

  const auto mapper = [](unsigned w, size_t num_data){
    tf::Executor executor(w);
    tf::Taskflow tf;
    std::vector<int> vec(num_data, 0);
    tf.for_each(
      vec.begin(), vec.end(), [] (int& v) { v = 64; } /*, group ? ::rand()%17 : 0*/
    );
    for(const auto v : vec) {
      REQUIRE(v == 0);
    }
    executor.run(tf);
    executor.wait_for_all();
    for(const auto v : vec) {
      REQUIRE(v == 64);
    }
  };

  const auto reducer = [](unsigned w, size_t num_data){
    tf::Executor executor(w);
    tf::Taskflow tf;
    std::vector<int> vec(num_data, 0);
    std::atomic<int> sum(0);
    tf.for_each(vec.begin(), vec.end(), [&](auto) { ++sum; }/*, group ? ::rand()%17 : 0*/);
    REQUIRE(sum == 0);
    executor.run(tf);
    executor.wait_for_all();
    REQUIRE(sum == vec.size());
  };

  // map
  SUBCASE("Map") {
    for(size_t num_data=1; num_data<=59049; num_data *= 3){
      mapper(W, num_data);
    }
  }

  // reduce
  SUBCASE("Reduce") {
    for(size_t num_data=1; num_data<=59049; num_data *= 3){
      reducer(W, num_data);
    }
  }
}

TEST_CASE("ParallelFor.1thread" * doctest::timeout(300)) {
  for_each(1);
}

TEST_CASE("ParallelFor.2threads" * doctest::timeout(300)) {
  for_each(2);
}

TEST_CASE("ParallelFor.3threads" * doctest::timeout(300)) {
  for_each(3);
}

TEST_CASE("ParallelFor.4threads" * doctest::timeout(300)) {
  for_each(4);
}

TEST_CASE("ParallelFor.5threads" * doctest::timeout(300)) {
  for_each(5);
}

TEST_CASE("ParallelFor.6threads" * doctest::timeout(300)) {
  for_each(6);
}

TEST_CASE("ParallelFor.7threads" * doctest::timeout(300)) {
  for_each(7);
}

TEST_CASE("ParallelFor.8threads" * doctest::timeout(300)) {
  for_each(8);
}

// --------------------------------------------------------
// Testcase: ParallelForOnIndex
// --------------------------------------------------------
void for_each_index(unsigned w) {
  
  using namespace std::chrono_literals;

  //auto exception_test = [] () {

  //  tf::Taskflow tf;

  //  // invalid index
  //  REQUIRE_THROWS(tf.for_each(0, 10, 0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0, 10, -1, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10, 0, 0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10, 0, 1, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0u, 10u, 0u, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10u, 0u, 0u, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10u, 0u, 1u, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0.0f, 10.0f, 0.0f, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0.0f, 10.0f, -1.0f, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10.0f, 0.0f, 0.0f, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10.0f, 0.0f, 1.0f, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0.0, 10.0, 0.0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0.0, 10.0, -1.0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10.0, 0.0, 0.0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(10.0, 0.0, 1.0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0, 0, 0, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0u, 0u, 0u, [] (auto) {}));
  //  REQUIRE_THROWS(tf.for_each(0.0, 0.0, 0.0, [] (auto) {}));
  //  
  //  // graceful case
  //  REQUIRE_NOTHROW(tf.for_each(0, 0, -1, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0, 0, 1, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0u, 0u, 1u, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0.0f, 0.0f, -1.0f, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0.0f, 0.0f, 1.0f, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0.0, 0.0, -1.0, [] (auto) {}));
  //  REQUIRE_NOTHROW(tf.for_each(0.0, 0.0, 1.0, [] (auto) {}));
  //};

  auto positive_integer_step = [] (unsigned w) {
    tf::Executor executor(w);
    for(int beg=-10; beg<=10; ++beg) {
      for(int end=beg; end<=10; ++end) {
        for(int s=1; s<=end-beg; ++s) {
          int n = 0;
          for(int b = beg; b<end; b+=s) {
            ++n;
          }
          //for(size_t c=0; c<10; c++) {
            tf::Taskflow tf;
            std::atomic<int> counter {0};
            tf.for_each_index(beg, end, s, [&] (auto) {
              counter.fetch_add(1, std::memory_order_relaxed);
            }/*, c*/);
            executor.run(tf);
            executor.wait_for_all();
            REQUIRE(n == counter);
          //}
        }
      }
    }
  };
  
  auto negative_integer_step = [] (unsigned w) {
    tf::Executor executor(w);
    for(int beg=10; beg>=-10; --beg) {
      for(int end=beg; end>=-10; --end) {
        for(int s=1; s<=beg-end; ++s) {
          int n = 0;
          for(int b = beg; b>end; b-=s) {
            ++n;
          }
          //for(size_t c=0; c<10; c++) {
            tf::Taskflow tf;
            std::atomic<int> counter {0};
            tf.for_each_index(beg, end, -s, [&] (auto) {
              counter.fetch_add(1, std::memory_order_relaxed);
            }/*, c*/);
            executor.run(tf);
            executor.wait_for_all();
            REQUIRE(n == counter);
          //}
        }
      }
    }
  };
  
  //auto positive_floating_step = [] (unsigned w) {
  //  tf::Executor executor(w);
  //  for(float beg=-10.0f; beg<=10.0f; ++beg) {
  //    for(float end=beg; end<=10.0f; ++end) {
  //      for(float s=1.0f; s<=end-beg; s+=0.1f) {
  //        int n = 0;
  //        if(beg < end) {
  //          for(float b = beg; b < end; b += s) {
  //            ++n;
  //          }
  //        }
  //        else if(beg > end) {
  //          for(float b = beg; b > end; b += s) {
  //            ++n;
  //          }
  //        }
  //        
  //        tf::Taskflow tf;
  //        std::atomic<int> counter {0};
  //        tf.for_each(beg, end, s, [&] (auto) {
  //          counter.fetch_add(1, std::memory_order_relaxed);
  //        });
  //        executor.run(tf);
  //        executor.wait_for_all();
  //        REQUIRE(n == counter);
  //      }
  //    }
  //  }
  //};
  //
  //auto negative_floating_step = [] (unsigned w) {
  //  tf::Executor executor(w);
  //  for(float beg=10.0f; beg>=-10.0f; --beg) {
  //    for(float end=beg; end>=-10.0f; --end) {
  //      for(float s=1.0f; s<=beg-end; s+=0.1f) {
  //        int n = 0;
  //        if(beg < end) {
  //          for(float b = beg; b < end; b += (-s)) {
  //            ++n;
  //          }
  //        }
  //        else if(beg > end) {
  //          for(float b = beg; b > end; b += (-s)) {
  //            ++n;
  //          }
  //        }
  //        tf::Taskflow tf;
  //        std::atomic<int> counter {0};
  //        tf.for_each(beg, end, -s, [&] (auto) {
  //          counter.fetch_add(1, std::memory_order_relaxed);
  //        });
  //        executor.run(tf);
  //        executor.wait_for_all();
  //        REQUIRE(n == counter);
  //      }
  //    }
  //  }
  //};
  
  //SUBCASE("Exception") {
  //  exception_test();
  //}

  SUBCASE("PositiveIntegerStep") {
    positive_integer_step(w);  
  }
  
  SUBCASE("NegativeIntegerStep") {
    negative_integer_step(w);  
  }
  
  //SUBCASE("PositiveFloatingStep") {
  //  positive_floating_step(w);  
  //}
  //
  //SUBCASE("NegativeFloatingStep") {
  //  negative_floating_step(w);  
  //}
}

TEST_CASE("ParallelForIndex.1thread" * doctest::timeout(300)) {
  for_each_index(1);
}

TEST_CASE("ParallelForIndex.2threads" * doctest::timeout(300)) {
  for_each_index(2);
}

TEST_CASE("ParallelForIndex.3threads" * doctest::timeout(300)) {
  for_each_index(3);
}

TEST_CASE("ParallelForIndex.4threads" * doctest::timeout(300)) {
  for_each_index(4);
}

TEST_CASE("ParallelForIndex.5threads" * doctest::timeout(300)) {
  for_each_index(5);
}

TEST_CASE("ParallelForIndex.6threads" * doctest::timeout(300)) {
  for_each_index(6);
}

TEST_CASE("ParallelForIndex.7threads" * doctest::timeout(300)) {
  for_each_index(7);
}

TEST_CASE("ParallelForIndex.8threads" * doctest::timeout(300)) {
  for_each_index(8);
}

// --------------------------------------------------------
// Testcase: Reduce
// --------------------------------------------------------
TEST_CASE("Reduce" * doctest::timeout(300)) {

  const auto plus_test = [](unsigned num_workers, auto &&data){
    tf::Executor executor(num_workers);
    tf::Taskflow tf;
    int result {0};
    std::iota(data.begin(), data.end(), 1);
    tf.reduce(data.begin(), data.end(), result, std::plus<int>());
    executor.run(tf).get();
    REQUIRE(result == std::accumulate(data.begin(), data.end(), 0, std::plus<int>()));
  };

  const auto multiply_test = [](unsigned num_workers, auto &&data){
    tf::Executor executor(num_workers);
    tf::Taskflow tf;
    std::fill(data.begin(), data.end(), 1.0);
    double result {2.0};
    tf.reduce(data.begin(), data.end(), result, std::multiplies<double>());
    executor.run(tf).get();
    REQUIRE(result == std::accumulate(data.begin(), data.end(), 2.0, std::multiplies<double>()));
  };

  const auto max_test = [](unsigned num_workers, auto &&data){
    tf::Executor executor(num_workers);
    tf::Taskflow tf;
    std::iota(data.begin(), data.end(), 1);
    int result {0};
    auto lambda = [](const auto& l, const auto& r){return std::max(l, r);};
    tf.reduce(data.begin(), data.end(), result, lambda);
    executor.run(tf).get();
    REQUIRE(result == std::accumulate(data.begin(), data.end(), 0, lambda));
  };

  const auto min_test = [](unsigned num_workers, auto &&data){
    tf::Executor executor(num_workers);
    tf::Taskflow tf;
    std::iota(data.begin(), data.end(), 1);
    int result {std::numeric_limits<int>::max()};
    auto lambda = [](const auto& l, const auto& r){return std::min(l, r);};
    tf.reduce(data.begin(), data.end(), result, lambda);
    executor.run(tf).get();
    REQUIRE(result == std::accumulate(
      data.begin(), data.end(), std::numeric_limits<int>::max(), lambda)
    );
  };

  for(unsigned w=1; w<=4; ++w){
    for(size_t j=0; j<=256; j=j*2+1){
      plus_test(w, std::vector<int>(j));
      plus_test(w, std::list<int>(j));

      multiply_test(w, std::vector<double>(j));
      multiply_test(w, std::list<double>(j));

      max_test(w, std::vector<int>(j));
      max_test(w, std::list<int>(j));

      min_test(w, std::vector<int>(j));
      min_test(w, std::list<int>(j));
    }
  }
}

// --------------------------------------------------------
// Testcase: ReduceMin
// --------------------------------------------------------
TEST_CASE("ReduceMin" * doctest::timeout(300)) {

  for(int w=1; w<=4; w++) {
    tf::Executor executor(w);
    for(int i=0; i<=65536; i = (i <= 1024) ? i + 1 : i*2 + 1) {
      tf::Taskflow tf;
      std::vector<int> data(i);
      int gold = std::numeric_limits<int>::max();
      int test = std::numeric_limits<int>::max();
      for(auto& d : data) {
        d = ::rand();
        gold = std::min(gold, d);
      }
      tf.reduce(data.begin(), data.end(), test, [] (int l, int r) {
        return std::min(l, r);
      });
      executor.run(tf).get();
      REQUIRE(test == gold);
    }
  }

}

// --------------------------------------------------------
// Testcase: ReduceMax
// --------------------------------------------------------
TEST_CASE("ReduceMax" * doctest::timeout(300)) {

  for(int w=1; w<=4; w++) {
    tf::Executor executor(w);
    for(int i=0; i<=65536; i = (i <= 1024) ? i + 1 : i*2 + 1) {
      tf::Taskflow tf;
      std::vector<int> data(i);
      int gold = std::numeric_limits<int>::min();
      int test = std::numeric_limits<int>::min();
      for(auto& d : data) {
        d = ::rand();
        gold = std::max(gold, d);
      }
      tf.reduce(data.begin(), data.end(), test, [](int l, int r){
        return std::max(l, r);
      });
      executor.run(tf).get();
      REQUIRE(test == gold);
    }
  }
}

// --------------------------------------------------------
// Testcase: JoinedSubflow
// -------------------------------------------------------- 

void joined_subflow(unsigned W) {

  using namespace std::literals::chrono_literals;
  
  SUBCASE("Trivial") {
    tf::Executor executor(W);
    tf::Taskflow tf;
    
    // empty flow with future
    tf::Task subflow3, subflow3_;
    //std::future<int> fu3, fu3_;
    std::atomic<int> fu3v{0}, fu3v_{0};
    
    // empty flow
    auto subflow1 = tf.emplace([&] (tf::Subflow& fb) {
      fu3v++;
      fb.join();
    }).name("subflow1");
    
    // nested empty flow
    auto subflow2 = tf.emplace([&] (tf::Subflow& fb) {
      fu3v++;
      fb.emplace([&] (tf::Subflow& fb) {
        fu3v++;
        fb.emplace( [&] (tf::Subflow& fb) {
          fu3v++;
          fb.join();
        }).name("subflow2_1_1");
      }).name("subflow2_1");
    }).name("subflow2");
    
    subflow3 = tf.emplace([&] (tf::Subflow& fb) {

      REQUIRE(fu3v == 4);

      fu3v++;
      fu3v_++;
      
      subflow3_ = fb.emplace([&] (tf::Subflow& fb) {
        REQUIRE(fu3v_ == 3);
        fu3v++;
        fu3v_++;
        //return 200;
        fb.join();
      });
      subflow3_.name("subflow3_");

      // hereafter we use 100us to avoid dangling reference ...
      auto s1 = fb.emplace([&] () { 
        fu3v_++;
        fu3v++;
      }).name("s1");
      
      auto s2 = fb.emplace([&] () {
        fu3v_++;
        fu3v++;
      }).name("s2");
      
      auto s3 = fb.emplace([&] () {
        fu3v++;
        REQUIRE(fu3v_ == 4);
      }).name("s3");

      s1.precede(subflow3_);
      s2.precede(subflow3_);
      subflow3_.precede(s3);

      REQUIRE(fu3v_ == 1);

      //return 100;
    });
    subflow3.name("subflow3");

    // empty flow to test future
    auto subflow4 = tf.emplace([&] () {
      fu3v++;
    }).name("subflow4");

    subflow1.precede(subflow2);
    subflow2.precede(subflow3);
    subflow3.precede(subflow4);

    executor.run(tf).get();
    // End of for loop
  }
  
  // Mixed intra- and inter- operations
  SUBCASE("Complex") {
    tf::Executor executor(W);
    tf::Taskflow tf;

    std::vector<int> data;
    int sum {0};

    auto A = tf.emplace([&data] () {
      for(int i=0; i<10; ++i) {
        data.push_back(1);
      }
    });

    std::atomic<size_t> count {0};

    auto B = tf.emplace([&count, &data, &sum](tf::Subflow& fb){

      //auto [src, tgt] = fb.reduce(data.begin(), data.end(), sum, std::plus<int>());
      auto task = fb.reduce(data.begin(), data.end(), sum, std::plus<int>());

      fb.emplace([&sum] () { REQUIRE(sum == 0); }).precede(task);

      task.precede(fb.emplace([&sum] () { REQUIRE(sum == 10); }));

      for(size_t i=0; i<10; i ++){
        ++count;
      }

      auto n = fb.emplace([&count](tf::Subflow& fb){

        REQUIRE(count == 20);
        ++count;

        auto prev = fb.emplace([&count](){
          REQUIRE(count == 21);
          ++count;
        });

        for(size_t i=0; i<10; i++){
          auto next = fb.emplace([&count, i](){
            REQUIRE(count == 22+i);
            ++count;
          });
          prev.precede(next);
          prev = next;
        }
      });

      for(size_t i=0; i<10; i++){
        fb.emplace([&count](){ ++count; }).precede(n);
      }
    });

    A.precede(B);

    executor.run(tf).get();
    REQUIRE(count == 32);
    REQUIRE(sum == 10);
    
  }
}

TEST_CASE("JoinedSubflow.1thread" * doctest::timeout(300)){
  joined_subflow(1);
}

TEST_CASE("JoinedSubflow.2threads" * doctest::timeout(300)){
  joined_subflow(2);
}

TEST_CASE("JoinedSubflow.3threads" * doctest::timeout(300)){
  joined_subflow(3);
}

TEST_CASE("JoinedSubflow.4threads" * doctest::timeout(300)){
  joined_subflow(4);
}

TEST_CASE("JoinedSubflow.5threads" * doctest::timeout(300)){
  joined_subflow(5);
}

TEST_CASE("JoinedSubflow.6threads" * doctest::timeout(300)){
  joined_subflow(6);
}

TEST_CASE("JoinedSubflow.7threads" * doctest::timeout(300)){
  joined_subflow(7);
}

TEST_CASE("JoinedSubflow.8threads" * doctest::timeout(300)){
  joined_subflow(8);
}

// --------------------------------------------------------
// Testcase: DetachedSubflow
// --------------------------------------------------------

void detached_subflow(unsigned W) {
  
  using namespace std::literals::chrono_literals;

  SUBCASE("Trivial") {
    tf::Executor executor(W);
    tf::Taskflow tf;
    
    // empty flow with future
    tf::Task subflow3, subflow3_;
    std::atomic<int> fu3v{0}, fu3v_{0};
    
    // empty flow
    auto subflow1 = tf.emplace([&] (tf::Subflow& fb) {
      fu3v++;
      fb.detach();
    }).name("subflow1");
    
    // nested empty flow
    auto subflow2 = tf.emplace([&] (tf::Subflow& fb) {
      fu3v++;
      fb.emplace([&] (tf::Subflow& fb) {
        fu3v++;
        fb.emplace( [&] (tf::Subflow& fb) {
          fu3v++;
          fb.join();
        }).name("subflow2_1_1");
        fb.detach();
      }).name("subflow2_1");
      fb.detach();
    }).name("subflow2");
    
    subflow3 = tf.emplace([&] (tf::Subflow& fb) {

      REQUIRE((fu3v >= 2 && fu3v <= 4));

      fu3v++;
      fu3v_++;
      
      subflow3_ = fb.emplace([&] (tf::Subflow& fb) {
        REQUIRE(fu3v_ == 3);
        fu3v++;
        fu3v_++;
        fb.join();
      });
      subflow3_.name("subflow3_");

      // hereafter we use 100us to avoid dangling reference ...
      auto s1 = fb.emplace([&] () { 
        fu3v_++;
        fu3v++;
      }).name("s1");
      
      auto s2 = fb.emplace([&] () {
        fu3v_++;
        fu3v++;
      }).name("s2");
      
      auto s3 = fb.emplace([&] () {
        fu3v++;
        REQUIRE(fu3v_ == 4);
      }).name("s3");

      s1.precede(subflow3_);
      s2.precede(subflow3_);
      subflow3_.precede(s3);

      REQUIRE(fu3v_ == 1);

      fb.detach();

      //return 100;
    });
    subflow3.name("subflow3");

    // empty flow to test future
    auto subflow4 = tf.emplace([&] () {
      REQUIRE((fu3v >= 3 && fu3v <= 9));
      fu3v++;
    }).name("subflow4");

    subflow1.precede(subflow2);
    subflow2.precede(subflow3);
    subflow3.precede(subflow4);

    executor.run(tf).get();

    REQUIRE(fu3v  == 10);
    REQUIRE(fu3v_ == 4);
    
  }
}

TEST_CASE("DetachedSubflow.1thread" * doctest::timeout(300)) {
  detached_subflow(1);
}

TEST_CASE("DetachedSubflow.2threads" * doctest::timeout(300)) {
  detached_subflow(2);
}

TEST_CASE("DetachedSubflow.3threads" * doctest::timeout(300)) {
  detached_subflow(3);
}

TEST_CASE("DetachedSubflow.4threads" * doctest::timeout(300)) {
  detached_subflow(4);
}

TEST_CASE("DetachedSubflow.5threads" * doctest::timeout(300)) {
  detached_subflow(5);
}

TEST_CASE("DetachedSubflow.6threads" * doctest::timeout(300)) {
  detached_subflow(6);
}

TEST_CASE("DetachedSubflow.7threads" * doctest::timeout(300)) {
  detached_subflow(7);
}

TEST_CASE("DetachedSubflow.8threads" * doctest::timeout(300)) {
  detached_subflow(8);
}


// --------------------------------------------------------
// Testcase: TreeSubflow
// -------------------------------------------------------- 
void detach_spawn(const int max_depth, std::atomic<int>& counter, int depth, tf::Subflow& subflow)  {
  if(depth < max_depth) {
    counter.fetch_add(1, std::memory_order_relaxed);
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      detach_spawn(max_depth, counter, depth, subflow); }
    );
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      detach_spawn(max_depth, counter, depth, subflow); }
    );
    subflow.detach();
  }
}

void join_spawn(const int max_depth, std::atomic<int>& counter, int depth, tf::Subflow& subflow)  {
  if(depth < max_depth) {
    counter.fetch_add(1, std::memory_order_relaxed);
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      join_spawn(max_depth, counter, depth, subflow); }
    );
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      join_spawn(max_depth, counter, depth, subflow); }
    );
  }
}

void mix_spawn(
  const int max_depth, std::atomic<int>& counter, int depth, tf::Subflow& subflow
) {

  if(depth < max_depth) {
    auto ret = counter.fetch_add(1, std::memory_order_relaxed);
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      mix_spawn(max_depth, counter, depth, subflow); }
    ).name(std::string("left") + std::to_string(ret%2));
    subflow.emplace([&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
      mix_spawn(max_depth, counter, depth, subflow); }
    ).name(std::string("right") + std::to_string(ret%2));
    if(ret % 2) {
      subflow.detach();
    }
  }
}

TEST_CASE("TreeSubflow" * doctest::timeout(300)) {

  SUBCASE("AllDetach") {
    constexpr int max_depth {10};
    for(int W=1; W<=4; W++) {
      std::atomic<int> counter {0};
      tf::Taskflow tf;
      tf.emplace([&](tf::Subflow& subflow){ 
        detach_spawn(max_depth, counter, 0, subflow); 
      });

      tf::Executor executor(W);
      executor.run(tf).get();
      REQUIRE(counter == (1<<max_depth) - 1);
    }
  }


  SUBCASE("AllJoin") {
    constexpr int max_depth {10};
    for(int W=1; W<=4; W++) {
      std::atomic<int> counter {0};
      tf::Taskflow tf;
      tf.emplace([&](tf::Subflow& subflow){ 
        join_spawn(max_depth, counter, 0, subflow); 
      });
      tf::Executor executor(W);
      executor.run(tf).get();
      REQUIRE(counter == (1<<max_depth) - 1);
    }
  }

  SUBCASE("Mix") {
    constexpr int max_depth {10};
    for(int W=1; W<=4; W++) {
      std::atomic<int> counter {0};
      tf::Taskflow tf;
      tf.emplace([&](tf::Subflow& subflow){ 
        mix_spawn(max_depth, counter, 0, subflow); 
      }).name("top task");

      tf::Executor executor(W);
      executor.run(tf).get();
      REQUIRE(counter == (1<<max_depth) - 1);
    }
  }
}

// --------------------------------------------------------
// Testcase: FibSubflow
// --------------------------------------------------------
int fibonacci_spawn(int n, tf::Subflow& sbf) {
  if (n < 2) return n;
  int res1, res2;
  sbf.emplace([&res1, n] (tf::Subflow& sbf) { res1 = fibonacci_spawn(n - 1, sbf); } );
  sbf.emplace([&res2, n] (tf::Subflow& sbf) { res2 = fibonacci_spawn(n - 2, sbf); } );
  REQUIRE(sbf.joinable() == true);
  sbf.join();
  REQUIRE(sbf.joinable() == false);
  return res1 + res2;
}

void fibonacci(size_t W) {

  int N = 20;
  int res = -1;  // result

  tf::Executor executor(W);
  tf::Taskflow taskflow;

  taskflow.emplace([&res, N] (tf::Subflow& sbf) { 
    res = fibonacci_spawn(N, sbf);  
  });

  executor.run(taskflow).wait();

  REQUIRE(res == 6765);
}

TEST_CASE("FibSubflow.1thread") {
  fibonacci(1);
}

TEST_CASE("FibSubflow.2threads") {
  fibonacci(2);
}

TEST_CASE("FibSubflow.4threads") {
  fibonacci(4);
}

TEST_CASE("FibSubflow.5threads") {
  fibonacci(5);
}

TEST_CASE("FibSubflow.6threads") {
  fibonacci(6);
}

TEST_CASE("FibSubflow.7threads") {
  fibonacci(7);
}

TEST_CASE("FibSubflow.8threads") {
  fibonacci(8);
}

// --------------------------------------------------------
// Testcase: Composition
// --------------------------------------------------------
TEST_CASE("Composition-1" * doctest::timeout(300)) {

  for(unsigned w=1; w<=8; ++w) {

    tf::Executor executor(w);

    tf::Taskflow f0;

    int cnt {0};

    auto A = f0.emplace([&cnt](){ ++cnt; });
    auto B = f0.emplace([&cnt](){ ++cnt; });
    auto C = f0.emplace([&cnt](){ ++cnt; });
    auto D = f0.emplace([&cnt](){ ++cnt; });
    auto E = f0.emplace([&cnt](){ ++cnt; });

    A.precede(B);
    B.precede(C);
    C.precede(D);
    D.precede(E);

    tf::Taskflow f1;
    
    // module 1
    std::tie(A, B, C, D, E) = f1.emplace(
      [&cnt] () { ++cnt; },
      [&cnt] () { ++cnt; },
      [&cnt] () { ++cnt; },
      [&cnt] () { ++cnt; },
      [&cnt] () { ++cnt; }
    );
    A.precede(B);
    B.precede(C);
    C.precede(D);
    D.precede(E);
    auto m1_1 = f1.composed_of(f0);
    E.precede(m1_1);
    
    executor.run(f1).get();
    REQUIRE(cnt == 10);

    cnt = 0;
    executor.run_n(f1, 100).get();
    REQUIRE(cnt == 10 * 100);

    auto m1_2 = f1.composed_of(f0);
    m1_1.precede(m1_2);
    
    for(int n=0; n<100; n++) {
      cnt = 0;
      executor.run_n(f1, n).get();
      REQUIRE(cnt == 15*n);
    }

    cnt = 0;
    for(int n=0; n<100; n++) {
      executor.run(f1);
    }
    
    executor.wait_for_all();

    REQUIRE(cnt == 1500);
  }
}

// TESTCASE: composition-2
TEST_CASE("Composition-2" * doctest::timeout(300)) {

  for(unsigned w=1; w<=8; ++w) {

    tf::Executor executor(w);

    int cnt {0};
    
    // level 0 (+5)
    tf::Taskflow f0;

    auto A = f0.emplace([&cnt](){ ++cnt; });
    auto B = f0.emplace([&cnt](){ ++cnt; });
    auto C = f0.emplace([&cnt](){ ++cnt; });
    auto D = f0.emplace([&cnt](){ ++cnt; });
    auto E = f0.emplace([&cnt](){ ++cnt; });

    A.precede(B);
    B.precede(C);
    C.precede(D);
    D.precede(E);

    // level 1 (+10)
    tf::Taskflow f1;
    auto m1_1 = f1.composed_of(f0);
    auto m1_2 = f1.composed_of(f0);
    m1_1.precede(m1_2);

    // level 2 (+20)
    tf::Taskflow f2;
    auto m2_1 = f2.composed_of(f1);
    auto m2_2 = f2.composed_of(f1);
    m2_1.precede(m2_2);
    
    // synchronous run
    for(int n=0; n<100; n++) {
      cnt = 0;
      executor.run_n(f2, n).get();
      REQUIRE(cnt == 20*n);
    }

    // asynchronous run
    cnt = 0;
    for(int n=0; n<100; n++) {
      executor.run(f2);
    }
    executor.wait_for_all();
    REQUIRE(cnt == 100*20);
  }
}

// TESTCASE: composition-3
TEST_CASE("Composition-3" * doctest::timeout(300)) {
  
  for(unsigned w=1; w<=8; ++w) {
  
    tf::Executor executor(w);

    int cnt {0};
    
    // level 0 (+2)
    tf::Taskflow f0;

    auto A = f0.emplace([&cnt](){ ++cnt; });
    auto B = f0.emplace([&cnt](){ ++cnt; });

    A.precede(B);

    // level 1 (+4)
    tf::Taskflow f1;
    auto m1_1 = f1.composed_of(f0);
    auto m1_2 = f1.composed_of(f0);
    m1_1.precede(m1_2);

    // level 2 (+8)
    tf::Taskflow f2;
    auto m2_1 = f2.composed_of(f1);
    auto m2_2 = f2.composed_of(f1);
    m2_1.precede(m2_2);

    // level 3 (+16)
    tf::Taskflow f3;
    auto m3_1 = f3.composed_of(f2);
    auto m3_2 = f3.composed_of(f2);
    m3_1.precede(m3_2);

    // synchronous run
    for(int n=0; n<100; n++) {
      cnt = 0;
      executor.run_n(f3, n).get();
      REQUIRE(cnt == 16*n);
    }

    // asynchronous run
    cnt = 0;
    for(int n=0; n<100; n++) {
      executor.run(f3);
    }
    executor.wait_for_all();
    REQUIRE(cnt == 16*100);
  }
}

// --------------------------------------------------------
// Testcase: Observer 
// -------------------------------------------------------- 

void observer(unsigned w) {

  tf::Executor executor(w);

  auto observer = executor.make_observer<tf::ChromeObserver>();    

  tf::Taskflow taskflowA;
  std::vector<tf::Task> tasks;
  // Static tasking 
  for(auto i=0; i < 64; i ++) {
    tasks.emplace_back(taskflowA.emplace([](){}));
  }

  // Randomly specify dependency
  for(auto i=0; i < 64; i ++) {
    for(auto j=i+1; j < 64; j++) {
      if(rand()%2 == 0) {
        tasks[i].precede(tasks[j]);
      }
    }
  }

  executor.run_n(taskflowA, 16).get();

  REQUIRE(observer->num_tasks() == 64*16);

  observer->clear();
  REQUIRE(observer->num_tasks() == 0);
  tasks.clear();
  
}

TEST_CASE("Observer.1thread" * doctest::timeout(300)) {
  observer(1);
}

TEST_CASE("Observer.2threads" * doctest::timeout(300)) {
  observer(2);
}

TEST_CASE("Observer.3threads" * doctest::timeout(300)) {
  observer(3);
}

TEST_CASE("Observer.4threads" * doctest::timeout(300)) {
  observer(4);
}

// --------------------------------------------------------
// Testcase: Conditional Tasking
// -------------------------------------------------------- 
void conditional_spawn(
  std::atomic<int>& counter, 
  const int max_depth, 
  int depth, 
  tf::Subflow& subflow
)  {
  if(depth < max_depth) {
    for(int i=0; i<2; i++) {
      auto A = subflow.emplace([&](){ counter++; });
      auto B = subflow.emplace(
        [&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
          conditional_spawn(counter, max_depth, depth, subflow); 
      });
      auto C = subflow.emplace(
        [&, max_depth, depth=depth+1](tf::Subflow& subflow){ 
          conditional_spawn(counter, max_depth, depth, subflow); 
        }
      );

      auto cond = subflow.emplace([depth](){ 
        if(depth%2) return 1;
        else return 0; 
      }).precede(B, C);
      A.precede(cond);
    }
  }
}

void loop_cond(unsigned w) {

  tf::Executor executor(w);
  tf::Taskflow taskflow;

  int counter = -1;
  int state   = 0;

  auto A = taskflow.emplace([&] () { counter = 0; });
  auto B = taskflow.emplace([&] () mutable { 
      REQUIRE((++counter % 100) == (++state % 100));
      return counter < 100 ? 0 : 1; 
  });
  auto C = taskflow.emplace(
    [&] () { 
      REQUIRE(counter == 100); 
      counter = 0;
  });

  A.precede(B);
  B.precede(B, C);

  REQUIRE(A.num_strong_dependents() == 0);
  REQUIRE(A.num_weak_dependents() == 0);
  REQUIRE(A.num_dependents() == 0);

  REQUIRE(B.num_strong_dependents() == 1);
  REQUIRE(B.num_weak_dependents() == 1);
  REQUIRE(B.num_dependents() == 2);

  executor.run(taskflow).wait();
  REQUIRE(counter == 0);
  REQUIRE(state == 100);

  executor.run(taskflow);
  executor.run(taskflow);
  executor.run(taskflow);
  executor.run(taskflow);
  executor.run_n(taskflow, 10);
  executor.wait_for_all();

  REQUIRE(state == 1500);
}

TEST_CASE("LoopCond.1thread" * doctest::timeout(300)) {
  loop_cond(1);
}

TEST_CASE("LoopCond.2threads" * doctest::timeout(300)) {
  loop_cond(2);
}

TEST_CASE("LoopCond.3threads" * doctest::timeout(300)) {
  loop_cond(3);
}

TEST_CASE("LoopCond.4threads" * doctest::timeout(300)) {
  loop_cond(4);
}

// ----------------------------------------------------------------------------
// Testcase: FlipCoinCond
// ----------------------------------------------------------------------------
void flip_coin_cond(unsigned w) {

  tf::Taskflow taskflow;
  
  size_t rounds = 10000;
  size_t steps = 0;
  size_t total_steps = 0;
  double average_steps = 0.0;

  auto A = taskflow.emplace( [&](){ steps = 0; } );
  auto B = taskflow.emplace( [&](){ ++steps; return std::rand()%2; } );
  auto C = taskflow.emplace( [&](){ return std::rand()%2; } );
  auto D = taskflow.emplace( [&](){ return std::rand()%2; } );
  auto E = taskflow.emplace( [&](){ return std::rand()%2; } );
  auto F = taskflow.emplace( [&](){ return std::rand()%2; } );
  auto G = taskflow.emplace( [&]() mutable { 
      //++N;  // a new round
      total_steps += steps;
      //avg = (double)accu/N;
      //std::cout << "round " << N << ": steps=" << steps 
      //                           << " accumulated_steps=" << accu 
      //                           << " average_steps=" << avg << '\n';
    }
  );

  A.precede(B).name("init");
  B.precede(C, B).name("flip-coin-1");
  C.precede(D, B).name("flip-coin-2");
  D.precede(E, B).name("flip-coin-3");
  E.precede(F, B).name("flip-coin-4");
  F.precede(G, B).name("flip-coin-5");

  //taskflow.dump(std::cout);

  tf::Executor executor(w);

  executor.run_n(taskflow, rounds).wait();

  average_steps = total_steps / (double)rounds;
  
  REQUIRE(std::fabs(average_steps-32.0)<1.0);

  //taskflow.dump(std::cout);
}

TEST_CASE("FlipCoinCond.1thread" * doctest::timeout(300)) {
  flip_coin_cond(1);
}

TEST_CASE("FlipCoinCond.2threads" * doctest::timeout(300)) {
  flip_coin_cond(2);
}

TEST_CASE("FlipCoinCond.3threads" * doctest::timeout(300)) {
  flip_coin_cond(3);
}

TEST_CASE("FlipCoinCond.4threads" * doctest::timeout(300)) {
  flip_coin_cond(4);
}

// ----------------------------------------------------------------------------
// Testcase: CyclicCondition
// ----------------------------------------------------------------------------
void cyclic_cond(unsigned w) {
  tf::Executor executor(w);

  //      ____________________
  //      |                  | 
  //      v                  |
  // S -> A -> Branch -> many branches -> T
  //  
  // Make sure each branch will be passed through exactly once
  // and the T (target) node will also be passed

  tf::Taskflow flow;
  auto S = flow.emplace([](){});
  
  int num_iterations = 0;
  const int total_iteration = 1000;
  auto A = flow.emplace([&](){ num_iterations ++; });
  S.precede(A);

  int sel = 0;
  bool pass_T = false;
  std::vector<bool> pass(total_iteration, false);
  auto T = flow.emplace([&](){ 
    REQUIRE(num_iterations == total_iteration); pass_T=true; }
  );
  auto branch = flow.emplace([&](){ return sel++; });
  A.precede(branch);
  for(size_t i=0; i<total_iteration; i++) {
    auto t = flow.emplace([&, i](){ 
      if(num_iterations < total_iteration) {
        REQUIRE(!pass[i]);
        pass[i] = true;
        return 0; 
      }
      // The last node will come to here (last iteration) 
      REQUIRE(!pass[i]);
      pass[i] = true;
      return 1; 
    });
    branch.precede(t);
    t.precede(A);
    t.precede(T);
  }

  executor.run(flow).get();

  REQUIRE(pass_T);
  for(size_t i=0; i<pass.size(); i++) {
    REQUIRE(pass[i]);
  }
}

TEST_CASE("CyclicCond.1thread" * doctest::timeout(300)) {
  cyclic_cond(1);
}

TEST_CASE("CyclicCond.2threads" * doctest::timeout(300)) {
  cyclic_cond(2);
}

TEST_CASE("CyclicCond.3threads" * doctest::timeout(300)) {
  cyclic_cond(3);
}

TEST_CASE("CyclicCond.4threads" * doctest::timeout(300)) {
  cyclic_cond(4);
}

TEST_CASE("CyclicCond.5threads" * doctest::timeout(300)) {
  cyclic_cond(5);
}

TEST_CASE("CyclicCond.6threads" * doctest::timeout(300)) {
  cyclic_cond(6);
}

TEST_CASE("CyclicCond.7threads" * doctest::timeout(300)) {
  cyclic_cond(7);
}

TEST_CASE("CyclicCond.8threads" * doctest::timeout(300)) {
  cyclic_cond(8);
}

// ----------------------------------------------------------------------------
// BTreeCond
// ----------------------------------------------------------------------------
TEST_CASE("BTreeCondition" * doctest::timeout(300)) {
  for(unsigned w=1; w<=8; ++w) {
    for(int l=1; l<12; l++) {
      tf::Taskflow flow;
      std::vector<tf::Task> prev_tasks;
      std::vector<tf::Task> tasks;
      
      std::atomic<int> counter {0};
      int level = l;
    
      for(int i=0; i<level; i++) {
        tasks.clear();
        for(int j=0; j< (1<<i); j++) {
          if(i % 2 == 0) {
            tasks.emplace_back(flow.emplace([&](){ counter++; }) );
          }
          else {
            if(j%2) {
              tasks.emplace_back(flow.emplace([](){ return 1; }));
            }
            else {
              tasks.emplace_back(flow.emplace([](){ return 0; }));
            }
          }
        }
        
        for(size_t j=0; j<prev_tasks.size(); j++) {
          prev_tasks[j].precede(tasks[2*j]    );
          prev_tasks[j].precede(tasks[2*j + 1]);
        }
        tasks.swap(prev_tasks);
      }
    
      tf::Executor executor(w);
      executor.run(flow).wait();
    
      REQUIRE(counter == (1<<((level+1)/2)) - 1);
    }
  }
}

//             ---- > B
//             |
//  A -> Cond -
//             |
//             ---- > C

TEST_CASE("DynamicBTreeCondition" * doctest::timeout(300)) {
  for(unsigned w=1; w<=8; ++w) {
    std::atomic<int> counter {0};
    constexpr int max_depth = 6;
    tf::Taskflow flow;
    flow.emplace([&](tf::Subflow& subflow) { 
      counter++; 
      conditional_spawn(counter, max_depth, 0, subflow); }
    );
    tf::Executor executor(w);
    executor.run_n(flow, 4).get();
    // Each run increments the counter by (2^(max_depth+1) - 1)
    REQUIRE(counter.load() == ((1<<(max_depth+1)) - 1)*4);
  }
}

//        ______
//       |      |
//       v      |
//  S -> A -> cond  

void nested_cond(unsigned w) {

  const int outer_loop = 3;
  const int mid_loop = 4;
  const int inner_loop = 5;

  int counter {0};
  tf::Taskflow flow;
  auto S = flow.emplace([](){});
  auto A = flow.emplace([&] (tf::Subflow& subflow) mutable {
    //         ___________
    //        |           |
    //        v           |
    //   S -> A -> B -> cond 
    auto S = subflow.emplace([](){ });
    auto A = subflow.emplace([](){ }).succeed(S);
    auto B = subflow.emplace([&](tf::Subflow& subflow){ 

      //         ___________
      //        |           |
      //        v           |
      //   S -> A -> B -> cond 
      //        |
      //        -----> C
      //        -----> D
      //        -----> E

      auto S = subflow.emplace([](){});
      auto A = subflow.emplace([](){}).succeed(S);
      auto B = subflow.emplace([&](){ counter++; }).succeed(A);
      subflow.emplace([&, repeat=0]() mutable {
        if(repeat ++ < inner_loop) 
          return 0;
  
        repeat = 0;
        return 1;
      }).succeed(B).precede(A).name("cond");
  
      // Those are redundant tasks
      subflow.emplace([](){}).succeed(A).name("C");
      subflow.emplace([](){}).succeed(A).name("D");
      subflow.emplace([](){}).succeed(A).name("E");
    }).succeed(A);
    subflow.emplace([&, repeat=0]() mutable {
      if(repeat ++ < mid_loop) 
        return 0;
  
      repeat = 0;
      return 1;
    }).succeed(B).precede(A).name("cond");
  
  }).succeed(S);
  
  flow.emplace(
    [&, repeat=0]() mutable {
      if(repeat ++ < outer_loop) {
        return 0;
      }
  
      repeat = 0;
      return 1;
    }
  ).succeed(A).precede(A);

  tf::Executor executor(w);
  const int repeat = 10;
  executor.run_n(flow, repeat).get();

  REQUIRE(counter == (inner_loop+1)*(mid_loop+1)*(outer_loop+1)*repeat);
}

TEST_CASE("NestedCond.1thread" * doctest::timeout(300)) {
  nested_cond(1);
}

TEST_CASE("NestedCond.2threads" * doctest::timeout(300)) {
  nested_cond(2);
}

TEST_CASE("NestedCond.3threads" * doctest::timeout(300)) {
  nested_cond(3);
}

TEST_CASE("NestedCond.4threads" * doctest::timeout(300)) {
  nested_cond(4);
}

TEST_CASE("NestedCond.5threads" * doctest::timeout(300)) {
  nested_cond(5);
}

TEST_CASE("NestedCond.6threads" * doctest::timeout(300)) {
  nested_cond(6);
}

TEST_CASE("NestedCond.7threads" * doctest::timeout(300)) {
  nested_cond(7);
}

TEST_CASE("NestedCond.8threads" * doctest::timeout(300)) {
  nested_cond(8);
}

//         ________________
//        |  ___   ______  |
//        | |   | |      | |
//        v v   | v      | |
//   S -> A -> cond1 -> cond2 -> D
//               |
//                ----> B

void cond2cond(unsigned w) {

  const int repeat = 10;
  tf::Taskflow flow;

  int num_visit_A {0};
  int num_visit_C1 {0};
  int num_visit_C2 {0};

  int iteration_C1 {0};
  int iteration_C2 {0};

  auto S = flow.emplace([](){});
  auto A = flow.emplace([&](){ num_visit_A++; }).succeed(S);
  auto cond1 = flow.emplace([&]() mutable {
    num_visit_C1++;
    iteration_C1++;
    if(iteration_C1 == 1) return 0;
    return 1;
  }).succeed(A).precede(A);

  auto cond2 = flow.emplace([&]() mutable {
    num_visit_C2 ++;
    return iteration_C2++;
  }).succeed(cond1).precede(cond1, A);

  flow.emplace([](){ REQUIRE(false); }).succeed(cond1).name("B");
  flow.emplace([&](){
    iteration_C1 = 0;
    iteration_C2 = 0;
  }).succeed(cond2).name("D");

  tf::Executor executor(w);
  executor.run_n(flow, repeat).get();
  
  REQUIRE(num_visit_A  == 3*repeat);
  REQUIRE(num_visit_C1 == 4*repeat);
  REQUIRE(num_visit_C2 == 3*repeat);
  
}

TEST_CASE("Cond2Cond.1thread" * doctest::timeout(300)) {
  cond2cond(1);
}

TEST_CASE("Cond2Cond.2threads" * doctest::timeout(300)) {
  cond2cond(2);
}

TEST_CASE("Cond2Cond.3threads" * doctest::timeout(300)) {
  cond2cond(3);
}

TEST_CASE("Cond2Cond.4threads" * doctest::timeout(300)) {
  cond2cond(4);
}

TEST_CASE("Cond2Cond.5threads" * doctest::timeout(300)) {
  cond2cond(5);
}

TEST_CASE("Cond2Cond.6threads" * doctest::timeout(300)) {
  cond2cond(6);
}

TEST_CASE("Cond2Cond.7threads" * doctest::timeout(300)) {
  cond2cond(7);
}

TEST_CASE("Cond2Cond.8threads" * doctest::timeout(300)) {
  cond2cond(8);
}


void hierarchical_condition(unsigned w) {
  
  tf::Executor executor(w);
  tf::Taskflow tf0("c0");
  tf::Taskflow tf1("c1");
  tf::Taskflow tf2("c2");
  tf::Taskflow tf3("top");

  int c1, c2, c2_repeat;

  auto c1A = tf1.emplace( [&](){ c1=0; } );
  auto c1B = tf1.emplace( [&, state=0] () mutable {
    REQUIRE(state++ % 100 == c1 % 100);
  });
  auto c1C = tf1.emplace( [&](){ return (++c1 < 100) ? 0 : 1; });

  c1A.precede(c1B);
  c1B.precede(c1C);
  c1C.precede(c1B);
  c1A.name("c1A");
  c1B.name("c1B");
  c1C.name("c1C");
  
  auto c2A = tf2.emplace( [&](){ REQUIRE(c2 == 100); c2 = 0; } );
  auto c2B = tf2.emplace( [&, state=0] () mutable { 
      REQUIRE((state++ % 100) == (c2 % 100)); 
  });
  auto c2C = tf2.emplace( [&](){ return (++c2 < 100) ? 0 : 1; });

  c2A.precede(c2B);
  c2B.precede(c2C);
  c2C.precede(c2B);
  c2A.name("c2A");
  c2B.name("c2B");
  c2C.name("c2C");

  auto init = tf3.emplace([&](){ 
    c1=c2=c2_repeat=0; 
  }).name("init");

  auto loop1 = tf3.emplace([&](){
    return (++c2 < 100) ? 0 : 1;
  }).name("loop1");

  auto loop2 = tf3.emplace([&](){
    c2 = 0;
    return ++c2_repeat < 100 ? 0 : 1;
  }).name("loop2");
  
  auto sync = tf3.emplace([&](){
    REQUIRE(c2==0);
    REQUIRE(c2_repeat==100);
    c2_repeat = 0;
  }).name("sync");

  auto grab = tf3.emplace([&](){ 
    REQUIRE(c1 == 100);
    REQUIRE(c2 == 0);
    REQUIRE(c2_repeat == 0);
  }).name("grab");

  auto mod0 = tf3.composed_of(tf0).name("module0");
  auto mod1 = tf3.composed_of(tf1).name("module1");
  auto sbf1 = tf3.emplace([&](tf::Subflow& sbf){
    auto sbf1_1 = sbf.emplace([](){}).name("sbf1_1");
    auto module1 = sbf.composed_of(tf1).name("module1");
    auto sbf1_2 = sbf.emplace([](){}).name("sbf1_2");
    sbf1_1.precede(module1);
    module1.precede(sbf1_2);
    sbf.join();
  }).name("sbf1");
  auto mod2 = tf3.composed_of(tf2).name("module2");

  init.precede(mod0, sbf1, loop1);
  loop1.precede(loop1, mod2);
  loop2.succeed(mod2).precede(loop1, sync);
  mod0.precede(grab);
  sbf1.precede(mod1);
  mod1.precede(grab);
  sync.precede(grab);

  executor.run(tf3);
  executor.run_n(tf3, 10);
  executor.wait_for_all();

  //tf3.dump(std::cout);
}

TEST_CASE("HierCondition.1thread" * doctest::timeout(300)) {
  hierarchical_condition(1);
}

TEST_CASE("HierCondition.2threads" * doctest::timeout(300)) {
  hierarchical_condition(2);
}

TEST_CASE("HierCondition.3threads" * doctest::timeout(300)) {
  hierarchical_condition(3);
}

TEST_CASE("HierCondition.4threads" * doctest::timeout(300)) {
  hierarchical_condition(4);
}

TEST_CASE("HierCondition.5threads" * doctest::timeout(300)) {
  hierarchical_condition(5);
}

TEST_CASE("HierCondition.6threads" * doctest::timeout(300)) {
  hierarchical_condition(6);
}

TEST_CASE("HierCondition.7threads" * doctest::timeout(300)) {
  hierarchical_condition(7);
}

TEST_CASE("HierCondition.8threads" * doctest::timeout(300)) {
  hierarchical_condition(8);
}

// ----------------------------------------------------------------------------
// CondSubflow
// ----------------------------------------------------------------------------

void condition_subflow(unsigned W) {

  tf::Taskflow taskflow;
  tf::Executor executor(W);

  const size_t I = 1000;

  std::vector<size_t> data(I);

  size_t i;

  auto init = taskflow.emplace([&](){ i = 0; }).name("init");

  auto subflow = taskflow.emplace([&](tf::Subflow& sf){
    sf.emplace([&, i](){ 
      REQUIRE(i<I);
      data[i] = i*(i+1)/2*123;; 
    }).name(std::to_string(i));
    sf.detach();
  }).name("subflow");

  auto cond = taskflow.emplace([&](){
    if(++i < I) return 0;
    return 1;
  }).name("cond");

  auto stop = taskflow.emplace([](){}).name("stop");

  init.precede(subflow);
  subflow.precede(cond);
  cond.precede(subflow, stop);

  executor.run(taskflow).wait();

  REQUIRE(taskflow.num_tasks() == 4 + I);

  for(size_t i=0; i<data.size(); ++i) {
    REQUIRE(data[i] == i*(i+1)/2*123);
    data[i] = 0;
  }
  
  executor.run_n(taskflow, 1);
  executor.run_n(taskflow, 10);
  executor.run_n(taskflow, 100);

  executor.wait_for_all();
  
  REQUIRE(taskflow.num_tasks() == 4 + I*100);

  for(size_t i=0; i<data.size(); ++i) {
    REQUIRE(data[i] == i*(i+1)/2*123);
  }

}

TEST_CASE("CondSubflow.1thread") {
  condition_subflow(1);
}

TEST_CASE("CondSubflow.2threads") {
  condition_subflow(2);
}

TEST_CASE("CondSubflow.3threads") {
  condition_subflow(3);
}

TEST_CASE("CondSubflow.4threads") {
  condition_subflow(4);
}

TEST_CASE("CondSubflow.5threads") {
  condition_subflow(5);
}

TEST_CASE("CondSubflow.6threads") {
  condition_subflow(6);
}

TEST_CASE("CondSubflow.7threads") {
  condition_subflow(7);
}

TEST_CASE("CondSubflow.8threads") {
  condition_subflow(8);
}

// --------------------------------------------------------
// Testcase: Async
// --------------------------------------------------------

void async(unsigned W) {

  tf::Executor executor(W);

  std::vector<tf::Future<std::optional<int>>> fus;

  std::atomic<int> counter(0);
  
  int N = 100000;

  for(int i=0; i<N; ++i) {
    fus.emplace_back(executor.async([&](){
      counter.fetch_add(1, std::memory_order_relaxed);
      return -2;
    }));
  }
  
  executor.wait_for_all();

  REQUIRE(counter == N);
  
  int c = 0; 
  for(auto& fu : fus) {
    c += fu.get().value();
  }

  REQUIRE(-c == 2*N);
}

TEST_CASE("Async.1thread" * doctest::timeout(300)) {
  async(1);  
}

TEST_CASE("Async.2threads" * doctest::timeout(300)) {
  async(2);  
}

TEST_CASE("Async.4threads" * doctest::timeout(300)) {
  async(4);  
}

TEST_CASE("Async.8threads" * doctest::timeout(300)) {
  async(8);  
}

TEST_CASE("Async.16threads" * doctest::timeout(300)) {
  async(16);  
}

// --------------------------------------------------------
// Testcase: NestedAsync
// --------------------------------------------------------

void nested_async(unsigned W) {

  tf::Executor executor(W);

  std::vector<tf::Future<std::optional<int>>> fus;

  std::atomic<int> counter(0);
  
  int N = 100000;

  for(int i=0; i<N; ++i) {
    fus.emplace_back(executor.async([&](){
      counter.fetch_add(1, std::memory_order_relaxed);
      executor.async([&](){
        counter.fetch_add(1, std::memory_order_relaxed);
        executor.async([&](){
          counter.fetch_add(1, std::memory_order_relaxed);
          executor.async([&](){
            counter.fetch_add(1, std::memory_order_relaxed);
          });
        });
      });
      return -2;
    }));
  }
  
  executor.wait_for_all();

  REQUIRE(counter == 4*N);
  
  int c = 0; 
  for(auto& fu : fus) {
    c += fu.get().value();
  }

  REQUIRE(-c == 2*N);
}

TEST_CASE("NestedAsync.1thread" * doctest::timeout(300)) {
  nested_async(1);  
}

TEST_CASE("NestedAsync.2threads" * doctest::timeout(300)) {
  nested_async(2);  
}

TEST_CASE("NestedAsync.4threads" * doctest::timeout(300)) {
  nested_async(4);  
}

TEST_CASE("NestedAsync.8threads" * doctest::timeout(300)) {
  nested_async(8);  
}

TEST_CASE("NestedAsync.16threads" * doctest::timeout(300)) {
  nested_async(16);  
}

// --------------------------------------------------------
// Testcase: MixedAsync
// --------------------------------------------------------

void mixed_async(unsigned W) {
  
  tf::Taskflow taskflow;
  tf::Executor executor(W);

  std::atomic<int> counter(0);

  int N = 1000;
  
  for(int i=0; i<N; i=i+1) {
    tf::Task A, B, C, D;
    std::tie(A, B, C, D) = taskflow.emplace(
      [&] () {
        executor.async([&](){
          counter.fetch_add(1, std::memory_order_relaxed);
        });
      },
      [&] () {
        executor.async([&](){
          counter.fetch_add(1, std::memory_order_relaxed);
        });
      },
      [&] () {
        executor.silent_async([&](){
          counter.fetch_add(1, std::memory_order_relaxed);
        });
      },
      [&] () {
        executor.silent_async([&](){
          counter.fetch_add(1, std::memory_order_relaxed);
        });
      }
    );

    A.precede(B, C);
    D.succeed(B, C);
  }
  
  executor.run(taskflow);
  executor.wait_for_all();

  REQUIRE(counter == 4*N);

}

TEST_CASE("MixedAsync.1thread" * doctest::timeout(300)) {
  mixed_async(1);  
}

TEST_CASE("MixedAsync.2threads" * doctest::timeout(300)) {
  mixed_async(2);  
}

TEST_CASE("MixedAsync.4threads" * doctest::timeout(300)) {
  mixed_async(4);  
}

TEST_CASE("MixedAsync.8threads" * doctest::timeout(300)) {
  mixed_async(8);  
}

TEST_CASE("MixedAsync.16threads" * doctest::timeout(300)) {
  mixed_async(16);  
}

// --------------------------------------------------------
// Testcase: SubflowAsync
// --------------------------------------------------------

void subflow_async(size_t W) {
  
  tf::Taskflow taskflow;
  tf::Executor executor(W);

  std::atomic<int> counter{0};

  auto A = taskflow.emplace(
    [&](){ counter.fetch_add(1, std::memory_order_relaxed); }
  );
  auto B = taskflow.emplace(
    [&](){ counter.fetch_add(1, std::memory_order_relaxed); }
  );
  
  taskflow.emplace(
    [&](){ counter.fetch_add(1, std::memory_order_relaxed); }
  );

  auto S1 = taskflow.emplace([&] (tf::Subflow& sf){
    for(int i=0; i<100; i++) {
      sf.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }
  });
  
  auto S2 = taskflow.emplace([&] (tf::Subflow& sf){
    sf.emplace([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    for(int i=0; i<100; i++) {
      sf.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }
  });
  
  taskflow.emplace([&] (tf::Subflow& sf){
    sf.emplace([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    for(int i=0; i<100; i++) {
      sf.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }
    sf.join();
  });
  
  taskflow.emplace([&] (tf::Subflow& sf){
    for(int i=0; i<100; i++) {
      sf.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }
    sf.join();
  });

  A.precede(S1, S2);
  B.succeed(S1, S2);

  executor.run(taskflow).wait();

  REQUIRE(counter == 405);
}

TEST_CASE("SubflowAsync.1thread") {
  subflow_async(1);
}

TEST_CASE("SubflowAsync.3threads") {
  subflow_async(3);
}

TEST_CASE("SubflowAsync.11threads") {
  subflow_async(11);
}

// --------------------------------------------------------
// Testcase: NestedSubflowAsync
// --------------------------------------------------------

void nested_subflow_async(size_t W) {
  
  tf::Taskflow taskflow;
  tf::Executor executor(W);

  std::atomic<int> counter{0};

  taskflow.emplace([&](tf::Subflow& sf1){ 

    for(int i=0; i<100; i++) {
      sf1.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
    }

    sf1.emplace([&](tf::Subflow& sf2){
      for(int i=0; i<100; i++) {
        sf2.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
        sf1.async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
      }

      sf2.emplace([&](tf::Subflow& sf3){
        for(int i=0; i<100; i++) {
          sf3.silent_async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
          sf2.silent_async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
          sf1.silent_async([&](){ counter.fetch_add(1, std::memory_order_relaxed); });
        }
      });
    });

    sf1.join();
    REQUIRE(counter == 600);
  });

  executor.run(taskflow).wait();
  REQUIRE(counter == 600);
}

TEST_CASE("NestedSubflowAsync.1thread") {
  nested_subflow_async(1);
}

TEST_CASE("NestedSubflowAsync.3threads") {
  nested_subflow_async(3);
}

TEST_CASE("NestedSubflowAsync.11threads") {
  nested_subflow_async(11);
}

// --------------------------------------------------------
// Testcase: CriticalSection
// --------------------------------------------------------

void critical_section(size_t W) {
  
  tf::Taskflow taskflow;
  tf::Executor executor(W);
  tf::CriticalSection section(1);
  
  int N = 1000;
  int counter = 0;

  for(int i=0; i<N; ++i) {
    tf::Task task = taskflow.emplace([&](){ counter++; })
                            .name(std::to_string(i));
    section.add(task);
  }

  executor.run(taskflow).wait();

  REQUIRE(counter == N);

  executor.run(taskflow);
  executor.run(taskflow);
  executor.run(taskflow);

  executor.wait_for_all();

  REQUIRE(counter == 4*N);
  REQUIRE(section.count() == 1);
}

TEST_CASE("CriticalSection.1thread") {
  critical_section(1);
}

TEST_CASE("CriticalSection.2threads") {
  critical_section(2);
}

TEST_CASE("CriticalSection.3threads") {
  critical_section(3);
}

TEST_CASE("CriticalSection.7threads") {
  critical_section(7);
}

TEST_CASE("CriticalSection.11threads") {
  critical_section(11);
}

TEST_CASE("CriticalSection.16threads") {
  critical_section(16);
}

// --------------------------------------------------------
// Testcase: Semaphore
// --------------------------------------------------------

void semaphore(size_t W) {

  tf::Executor executor(W);
  tf::Taskflow taskflow;
  tf::Semaphore semaphore(1);

  int N = 1000;
  int counter = 0;

  for(int i=0; i<N; i++) {
    auto f = taskflow.emplace([&](){ counter++; });
    auto t = taskflow.emplace([&](){ counter++; });
    f.precede(t);
    f.acquire(semaphore);
    t.release(semaphore);
  }
  
  executor.run(taskflow).wait();

  REQUIRE(counter == 2*N);

}

TEST_CASE("Semaphore.1thread") {
  semaphore(1);
}

TEST_CASE("Semaphore.2threads") {
  semaphore(2);
}

TEST_CASE("Semaphore.4threads") {
  semaphore(4);
}

TEST_CASE("Semaphore.8threads") {
  semaphore(8);
}

// --------------------------------------------------------
// Testcase: OverlappedSemaphore
// --------------------------------------------------------

void overlapped_semaphore(size_t W) {

  tf::Executor executor(W);
  tf::Taskflow taskflow;
  tf::Semaphore semaphore1(1);
  tf::Semaphore semaphore4(4);

  int N = 1000;
  int counter = 0;

  for(int i=0; i<N; i++) {
    auto task = taskflow.emplace([&](){ counter++; });
    task.acquire(semaphore1);
    task.acquire(semaphore4);
    task.release(semaphore1);
    task.release(semaphore4);
  }
  
  executor.run(taskflow).wait();

  REQUIRE(counter == N);
  REQUIRE(semaphore1.count() == 1);
  REQUIRE(semaphore4.count() == 4);
}

TEST_CASE("OverlappedSemaphore.1thread") {
  overlapped_semaphore(1);
}

TEST_CASE("OverlappedSemaphore.2threads") {
  overlapped_semaphore(2);
}

TEST_CASE("OverlappedSemaphore.4threads") {
  overlapped_semaphore(4);
}

TEST_CASE("OverlappedSemaphore.8threads") {
  overlapped_semaphore(8);
}


