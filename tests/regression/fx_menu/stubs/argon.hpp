#pragma once
#include <array>
#include <numbers>
#include <cstddef>
template<class T> struct ArgonHalf {
 std::array<T,2> a{};
 constexpr ArgonHalf()=default;
 constexpr ArgonHalf(T x):a{x,x}{}
 static constexpr ArgonHalf Load(const T* p){ArgonHalf r;r.a={p[0],p[1]};return r;}
 constexpr T& operator[](size_t i){return a[i];}
 constexpr T operator[](size_t i)const{return a[i];}
 constexpr size_t size()const{return 2;}
 template<class F> ArgonHalf each_lane_with_index(F f){auto r=*this;for(int i=0;i<2;++i)f(r.a[i],i);return r;}
 friend constexpr ArgonHalf operator+(ArgonHalf x,ArgonHalf y){for(int i=0;i<2;++i)x[i]+=y[i];return x;}
 friend constexpr ArgonHalf operator-(ArgonHalf x,ArgonHalf y){for(int i=0;i<2;++i)x[i]-=y[i];return x;}
 friend constexpr ArgonHalf operator*(ArgonHalf x,ArgonHalf y){for(int i=0;i<2;++i)x[i]*=y[i];return x;}
};
