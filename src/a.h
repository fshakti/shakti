#ifndef A_H
#define A_H
#define i0 int
#define g0 char
#define Uz int x
#define Uy int y
#define ZU static inline int
#define ZV static void
#define ia i0 a
#define it i0 t
#define ih i0 h
#define ii i0 i
#define ij i0 j
#define ik i0 k
#define il i0 l
#define im i0 m
#define in i0 n
#define cc g0 c
#define cd g0 d
#define ss g0*s
#define st g0*t
#ifdef SHAKTI_MINSIZE
#define MS __attribute__((minsize))
#else
#define MS
#endif
#define U(f,x,a...)ZU f(a){return({x;});}
#define Ui(f,x...)ZU f(ii){return({x;});}
#define Us(f,x)U(f,x,ss)
#define ns(f,x)U(f,x,in,ss)
#define ins(f,x)U(f,x,ii,in,ss)
#define f(f,x)U(f,x,Uz)
#define F(f,x)U(f,x,Uy,Uz)
#define jz(f,x)U(f,x,ij,Uz)
#define jyz(f,x)U(f,x,ij,Uy,Uz)
#define V(f,e,a...)ZV f(a){e;}
#define Vh(f,x)V(f,x,ih)
#define $(b,x)if(b){x;}else
#define S(i,a,b,c,d,e,x...)({it=i;!t?({a;}):1==t?({b;}):2==t?({c;}):3==t?({d;}):4==t?({e;}):({x;});})
#define R(a,e)({typeof(a)r=(a);e;r;})
#define P(b,e)if(b)return e;
#define Pr(b,x...)if(b)return({x;});
#define Pv(b)if(b)return;
#define W(b,x...)while(b){x;}
#define N(n,x){it=n;ii=0;for(;i<t;++i){x;}}
#define ISL_OMP_VEC_MIN 1000000
/* Work-sized OpenMP teams: ~125k elems/thread; hard-cap avoids many-core
 * fork/join collapse on large ivec add/sub/mul (e.g. 24 threads at 1M). */
#define ISL_OMP_VEC_CHUNK 125000
#define ISL_OMP_VEC_MAX_THREADS 16
#define ISL_MAT_SIMD_MIN_ELEMS 64
#define ISL_MAT_SIMD_K_MIN 8
#define ISL_MAT_OMP_ROWS_MIN 32
/* Apple Accelerate: prefer vDSP/cblas above these sizes (NEON below). */
#ifndef SHAKTI_ACCEL_VEC_MIN
#define SHAKTI_ACCEL_VEC_MIN 4096
#endif
#ifndef SHAKTI_ACCEL_GEMM_MIN_ELEMS
#define SHAKTI_ACCEL_GEMM_MIN_ELEMS 64
#endif
#ifndef SHAKTI_ACCEL_GEMM_K_MIN
#define SHAKTI_ACCEL_GEMM_K_MIN 8
#endif
#define i(n,x)for(it=n,i=0;i<t;++i){x;}
#define j(n,x)for(it=n,j=0;j<t;++j){x;}
#endif
