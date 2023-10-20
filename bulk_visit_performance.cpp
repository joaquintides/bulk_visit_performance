/* boost::concurrent_flat_map bulk visitation performance.
 *
 * Copyright 2023 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>

std::chrono::high_resolution_clock::time_point measure_start,measure_pause;

template<typename F>
double measure(F f)
{
  using namespace std::chrono;

  static const int              num_trials=10;
  static const milliseconds     min_time_per_trial(200);
  std::array<double,num_trials> trials;

  for(int i=0;i<num_trials;++i){
    int                               runs=0;
    high_resolution_clock::time_point t2;
    volatile decltype(f())            res; /* to avoid optimizing f() away */

    measure_start=high_resolution_clock::now();
    do{
      res=f();
      ++runs;
      t2=high_resolution_clock::now();
    }while(t2-measure_start<min_time_per_trial);
    trials[i]=duration_cast<duration<double>>(t2-measure_start).count()/runs;
  }

  std::sort(trials.begin(),trials.end());
  return std::accumulate(
    trials.begin()+2,trials.end()-2,0.0)/(trials.size()-4);
}

void pause_timing()
{
  measure_pause=std::chrono::high_resolution_clock::now();
}

void resume_timing()
{
  measure_start+=std::chrono::high_resolution_clock::now()-measure_pause;
}

#include <boost/bind/bind.hpp>
#include <boost/core/detail/splitmix64.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <iostream>
#include <random>

template<typename Map,typename Distribution>
class regular_visitor
{
public:
  regular_visitor(const Map& m_,const Distribution& dist_):m{m_},dist{dist_}{}

  template<typename URNG>
  void operator()(URNG& gen)
  {
    res+=(int)m.visit(dist(gen),[](const auto&){});
  }

  void flush(){}

  int res=0;

private:
  const Map&   m;
  Distribution dist;
};

template<typename Map,typename Distribution>
class bulk_visitor
{
public:
  bulk_visitor(const Map& m_,const Distribution& dist_):m{m_},dist{dist_}{}

  template<typename URNG>
  void operator()(URNG& gen)
  {
    keys[i++]=dist(gen);
    if(i==N)flush();
  }

  void flush()
  {
    res+=(int)m.visit(keys.begin(),keys.begin()+i,[](const auto&){});
    i=0;
  }

  int res=0;

private:
  static constexpr std::size_t N=Map::bulk_visit_size;

  const Map&        m;
  Distribution      dist;
  int               i=0;
  std::array<int,N> keys;
};

struct splitmix64_urng:boost::detail::splitmix64
{
  using boost::detail::splitmix64::splitmix64;
  using result_type=boost::uint64_t;

  static constexpr result_type (min)(){return 0u;}
  static constexpr result_type(max)()
  {return (std::numeric_limits<result_type>::max)();}
};

template<template<typename...> class Visitor>
struct visit_tester
{
  using result_type=std::size_t;

  template<typename Map>
  BOOST_NOINLINE result_type operator()(const Map& m,int N)const
  {
    using distribution_type=std::uniform_int_distribution<>;

    splitmix64_urng                gen(282472u);
    Visitor<Map,distribution_type> visit{m,distribution_type{0,2*N-1}};

    for(int i=0;i<N;++i)visit(gen);
    visit.flush();
    return visit.res;
  }
};

template<
  template<typename...> class Visitor1,template<typename...> class Visitor2
>
BOOST_NOINLINE void visit_test(const char* name1,const char* name2)
{
  std::cout<<"visit:"<<std::endl;
  std::cout<<"N;"<<name1<<";"<<name2<<std::endl;

  for(auto N:{3'000,25'000,600'000,10'000'000}){
    boost::concurrent_flat_map<int,int> m;
    m.reserve(std::size_t(N));
    for(int i=0;i<N;++i) m.insert({i,i});

    auto t=measure(boost::bind(visit_tester<Visitor1>(),boost::cref(m),N));
    std::cout<<N<<";"<<N/t/1E6<<";";
    t=measure(boost::bind(visit_tester<Visitor2>(),boost::cref(m),N));
    std::cout<<N/t/1E6<<std::endl;
  }
}

int main()
{
  visit_test<regular_visitor,bulk_visitor>("regular","bulk");
}
