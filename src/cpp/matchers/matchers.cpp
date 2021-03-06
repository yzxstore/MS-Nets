/* This code is from  https://github.com/kbatsos/CBMV;
 * with some modificaiton by CCJ */
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <iostream>
#include <stdlib.h>
#include <boost/python.hpp>
#include "boost/python/extract.hpp"
//#include "boost/python/numeric.hpp"
/* Updated by CCJ for Boost version 1.65:
 * Boost 1.65 removes boost/python/numeric.hpp 
 */
// BOOST_LIB_VERSION and BOOST_VERSION are defined in this header file;
#include <boost/version.hpp> 
//#if BOOST_VERSION >= 106300 // >= 1.63.0
#include "boost/python/numpy.hpp"
namespace np = boost::python::numpy;
//#else
//#include "boost/python/numeric.hpp"
//namespace np = boost::python::numeric;
//#endif

#include <numpy/ndarrayobject.h>
#include <algorithm>
#include <vector>
#include <stdint.h>
#include <assert.h>
#include <immintrin.h>
#include <omp.h>
#include <sstream>
// added by CCJ;
#include "../paramSetting.hpp"

using namespace std;
using namespace boost::python;
typedef uint8_t uint8;
typedef int16_t int16;
typedef unsigned long long int uint64;

/* float precision*/
typedef float _MY_FLOAT_ ;

/* double precision*/
//typedef double _MY_FLOAT_ ;
//typedef NPY_FLOAT64 NPY_FLOAT32;


PyObject* nccNister(PyObject* left, PyObject *right, int ndisp, int wsize){
    // Cast to pointer to Python Array object
    PyArrayObject* leftA = reinterpret_cast<PyArrayObject*>(left);
    PyArrayObject* rightA = reinterpret_cast<PyArrayObject*>(right);

    //Get the pointer to the data
    uint8 * leftp = reinterpret_cast<uint8*>(PyArray_DATA(leftA));
    uint8 * rightp = reinterpret_cast<uint8*>(PyArray_DATA(rightA));

    npy_intp *shape = PyArray_DIMS(leftA);
    npy_intp *costshape = new npy_intp[3];
    costshape[1] = shape[0]; costshape[2] = shape[1]; costshape[0] = ndisp;


    PyObject* cost = PyArray_SimpleNew(3, costshape, NPY_FLOAT32);

    _MY_FLOAT_* res_data = static_cast<_MY_FLOAT_*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(cost)));

    std::fill_n(res_data,shape[0]*shape[1]*ndisp,RAND_MAX);
    const int wc = wsize/2;
    const int sqwin = wsize*wsize;
    const int intgrrows = shape[0]+1;
    const int intgrcols = shape[1] +1;

    unsigned int * lintegral = (unsigned int *)calloc(intgrrows*intgrcols,sizeof(unsigned int));
    unsigned int * rintegral = (unsigned int *)calloc(intgrrows*intgrcols,sizeof(unsigned int));
    unsigned long long * sqlintegral = (unsigned long long *)calloc(intgrrows*intgrcols,sizeof(unsigned long long));
    unsigned long long * sqrintegral = (unsigned long long *)calloc(intgrrows*intgrcols,sizeof(unsigned long long));

#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		#pragma omp for
		for(int i=0; i<shape[0]; i++){
			const int row = i*shape[1];
			const int introw = (i+1)*intgrcols;
			for (int j=0; j<shape[1]; j++){
				const int intcol = j+1;
				lintegral[introw+intcol] = leftp[row+j];
				sqlintegral[introw+intcol] = leftp[row+j]*leftp[row+j];
				rintegral[introw+intcol] = rightp[row+j];
				sqrintegral[introw+intcol] =  rightp[row+j]* rightp[row+j];
			}
		}
    }



#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		for (int i=1; i< intgrrows; i++){
			const int row = i*intgrcols;
			const int prev_row = (i-1)*intgrcols;
			#pragma omp for
			for(int j=0; j<intgrcols;j++){
				lintegral[row+j] += lintegral[prev_row+j];
				rintegral[row+j] += rintegral[prev_row+j];
				sqlintegral[row+j] += sqlintegral[prev_row+j];
				sqrintegral[row+j] += sqrintegral[prev_row+j];
			}
		}
    }

#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		#pragma omp for
		for(int i=0; i<intgrrows; i++){
			const int row =  i*intgrcols;
			for(int j=1; j<intgrcols; j++){
				const int prev_col = j-1;
				lintegral[row+j] += lintegral[row+prev_col];
				rintegral[row+j] += rintegral[row+prev_col];
				sqlintegral[row+j] += sqlintegral[row+prev_col];
				sqrintegral[row+j] += sqrintegral[row+prev_col];
			}
		}
    }


    uint64* Al = (uint64 *)calloc(shape[0]*shape[1],sizeof(uint64));
    uint64* Ar = (uint64 *)calloc(shape[0]*shape[1],sizeof(uint64));
		// double precision is required;
    double * Cl = (double *)calloc(shape[0]*shape[1],sizeof(double));
    double * Cr = (double *)calloc(shape[0]*shape[1],sizeof(double));

#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		#pragma omp for
		for (int i=0; i< shape[0]-wsize;i++){
			const int row = (i+wc)*shape[1];
			const int t_row = i*intgrcols;
			const int b_row = (i+wsize)*intgrcols;
			for(int j=0; j< shape[1]-wsize; j++){
				const int col = j+wc;
				Al[row+col] = lintegral[b_row + j+wsize]+ lintegral[t_row + j]  - lintegral[b_row + j] - lintegral[t_row + j+wsize];
				Ar[row+col] = rintegral[b_row + j+wsize]+ rintegral[t_row + j] 	- rintegral[b_row + j] - rintegral[t_row + j+wsize];
				unsigned long long Bl = sqlintegral[b_row + j+wsize] + sqlintegral[t_row + j] - sqlintegral[b_row + j] - sqlintegral[t_row + j+wsize];
				unsigned long long Br = sqrintegral[b_row + j+wsize] + sqrintegral[t_row + j] - sqrintegral[b_row + j] - sqrintegral[t_row + j+wsize];


				Cl[ (i+wc)*shape[1]+(j+wc) ] = 1/(sqrt(sqwin*Bl - (double)( Al[row+col] )*( Al[row+col] ) ));
				Cr[ (i+wc)*shape[1]+(j+wc) ] = 1/(sqrt(sqwin*Br - (double)( Ar[row+col] )*( Ar[row+col]) ));
			}
		}
    }

#pragma omp parallel num_threads(THREADS_NUM_USED)
    {

		double * dslice = (double*)calloc(intgrrows*intgrcols,sizeof(double));
		#pragma omp for
		for (int d=0; d<ndisp; d++ ){

			const int d_row = d*shape[0]*shape[1];
			std::fill_n(dslice,intgrrows*intgrcols,0);
			for(int i=0; i<shape[0]; i++){
				const int row = i*shape[1];
				const int intgrrow = (i+1)*intgrcols;
				for(int j=d; j<shape[1]; j++){
					dslice[intgrrow + j+1] = leftp[row+j]*rightp[row+j-d];
				}
			}

			for(int i=1; i<intgrrows; i++ ){
				const int row = i*intgrcols;
				const int prev_row = (i-1)*intgrcols;
				for(int j=0; j<intgrcols; j++){
					dslice[row + j] += dslice[prev_row + j];
				}

			}

			for(int i=0; i<intgrrows; i++){
				const int row = i*intgrcols;
				for(int j=1; j<intgrcols; j++){
					const int prev_col = j-1;
					dslice[row + j] += dslice[row + prev_col];
				}
			}

			for (int i=0; i< shape[0]-wsize; i++){
				const int row = (i+wc)*shape[1];
				const int t_row = i*intgrcols;
				const int b_row = (i+wsize)*intgrcols;
				for(int j=d; j< shape[1]-wsize; j++){
					const int col = (j+wc);

					const double lD = dslice[b_row + j+wsize ] + dslice[t_row+j]
								 - dslice[b_row +j ] - dslice[t_row +j+wsize ];
								
			        	if( std::isfinite(Cl[ row+col])  && std::isfinite(Cr[ row+(j-d+wc)]) ){
			        		//res_data[d_row + row+col] = -(_MY_FLOAT_)(sqwin*lD- Al[row+col] * Ar[row+(j-d+wc)]) *Cl[ row+col ]*Cr[ row+(j-d+wc) ];
									//keep the doulbe precision during NCC matcher computation, 
									//but save the ncc cost result as float passed to python code;
                 double tmp = -(double)(sqwin*lD- Al[row+col] * Ar[row+(j-d+wc)]) *Cl[ row+col ]*Cr[ row+(j-d+wc) ];
								 res_data[d_row + row+col] = (_MY_FLOAT_)tmp;
								}
								else
			        		res_data[d_row + row+col] = (_MY_FLOAT_)1;

					}
				}

		}

		delete [] dslice;
    }

    delete [] lintegral;
    delete [] rintegral;
    delete [] sqlintegral;
    delete [] sqrintegral;

    delete [] Al;
    delete [] Ar;
    delete [] Cl;
    delete [] Cr;


    return cost;


}



PyObject* census(PyObject* left, PyObject *right, int ndisp, int wsize){
    // Cast to pointer to Python Array object
    PyArrayObject* leftA = reinterpret_cast<PyArrayObject*>(left);
    PyArrayObject* rightA = reinterpret_cast<PyArrayObject*>(right);

    //Get the pointer to the data
    uint8 * lefim = reinterpret_cast<uint8*>(PyArray_DATA(leftA));
    uint8 * rightim = reinterpret_cast<uint8*>(PyArray_DATA(rightA));


    npy_intp *shape = PyArray_DIMS(leftA);
    npy_intp *costshape = new npy_intp[3];
    costshape[0] = shape[0]; costshape[1] = shape[1]; costshape[2] = ndisp;


    PyObject* cost = PyArray_SimpleNew(3, costshape, NPY_FLOAT32);

    _MY_FLOAT_* res_data = static_cast<_MY_FLOAT_*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(cost)));

    std::fill_n(res_data,shape[0]*shape[1]*ndisp, RAND_MAX);

    int16 * leftp = (int16*)calloc(shape[0]*shape[1],sizeof(int16));
    int16 * rightp = (int16*)calloc(shape[0]*shape[1],sizeof(int16));

    #pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		#pragma omp for
		for (int i=0; i< shape[0]; i++){
			for(int j=0; j< shape[1]; j++){
				leftp[i*shape[1]+j] = lefim[i*shape[1]+j];
				rightp[i*shape[1]+j] = rightim[i*shape[1]+j];
			}
		}
    }

    int wc = wsize/2;
    int vecsize = wsize*wsize;
	if(vecsize%8 > 0)
		vecsize += 8-vecsize%8;
	int tchuncks = vecsize/8;

    __m128i* censustl = new __m128i[shape[0]*shape[1]*tchuncks];
    __m128i* censustr = new __m128i[shape[0]*shape[1]*tchuncks];

    #pragma omp parallel num_threads(THREADS_NUM_USED)
    {

		int16 * vecl = (int16*)calloc(vecsize,sizeof(int16));
		int16 * vecr = (int16*)calloc(vecsize,sizeof(int16));

		#pragma omp for
		for (int i=0; i< shape[0]-wsize;i++){
			for(int j=0; j< shape[1]-wsize; j++){
				for(int wh=0; wh<wsize; wh++){
					memcpy(&vecl[wh*wsize], &leftp[(i+wh)*shape[1]+j],wsize*sizeof(int16));
					memcpy(&vecr[wh*wsize], &rightp[(i+wh)*shape[1]+j],wsize*sizeof(int16));
				}
				//copy the center values to two registers
				__m128i centerl = _mm_set1_epi16(leftp[(i+wc)*shape[1]+(j+wc)]);
				__m128i centerr = _mm_set1_epi16(rightp[(i+wc)*shape[1]+(j+wc)]);

				for(int vp=0; vp<vecsize; vp+=8){
					__m128i lv = _mm_load_si128((__m128i const*) &vecl[vp]);
					__m128i rv = _mm_load_si128((__m128i const*) &vecr[vp]);
					censustl[(i+wc)*shape[1]*tchuncks + (j+wc)*tchuncks+vp/8 ]=_mm_cmplt_epi16(centerl,lv);
					censustr[(i+wc)*shape[1]*tchuncks + (j+wc)*tchuncks+vp/8 ]=_mm_cmplt_epi16(centerr,rv);
				}

			}
		}

	    delete [] vecl;
	    delete [] vecr;
    }





	#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
    	__m128i divc = _mm_set1_epi16(0);
		#pragma omp for
		for (int i=0; i< shape[0]-wsize; i++){
			for(int j=0; j< shape[1]-wsize; j++){

				int end = std::min(ndisp,(j+1));
				for(int d=0; d < end; d++){

					_MY_FLOAT_ sum =0;

					for(int vp=0; vp<vecsize; vp+=8){
						__m128i r1 =censustl[(i+wc)*shape[1]*tchuncks + (j+wc)*tchuncks+vp/8];
						__m128i r2 =censustr[(i+wc)*shape[1]*tchuncks + (j-d+wc)*tchuncks+vp/8];
						__m128i eqv = _mm_cmpeq_epi16(r1,r2);
						__m128i eqsub = _mm_sub_epi16(divc,eqv);
						__m128i hadd = _mm_hadd_epi16(eqsub,eqsub);
						hadd = _mm_hadd_epi16(hadd,hadd);
						hadd = _mm_hadd_epi16(hadd,hadd);
						sum += (_MY_FLOAT_) _mm_extract_epi16(hadd,0);


					}
					sum = vecsize - sum;

					res_data[(i+wc)*shape[1]*ndisp + (j+wc)*ndisp+ d] = sum;

				}
			}
		}
    }

    delete [] censustl;
    delete [] censustr;
    delete [] leftp;
    delete [] rightp;


    return cost;


}


PyObject* sadsob(PyObject* left, PyObject *right, int ndisp, int wsize){
    // Cast to pointer to Python Array object
    PyArrayObject* leftA = reinterpret_cast<PyArrayObject*>(left);
    PyArrayObject* rightA = reinterpret_cast<PyArrayObject*>(right);

    //Get the pointer to the data
    const _MY_FLOAT_ * leftp = reinterpret_cast<_MY_FLOAT_*>(PyArray_DATA(leftA));
    const _MY_FLOAT_ * rightp = reinterpret_cast<_MY_FLOAT_*>(PyArray_DATA(rightA));

    npy_intp *shape = PyArray_DIMS(leftA);
    npy_intp *costshape = new npy_intp[3];
    costshape[1] = shape[0]; costshape[2] = shape[1]; costshape[0] = ndisp;


    PyObject* cost = PyArray_SimpleNew(3, costshape, NPY_FLOAT32);

	const int integrrows = shape[0]+1;
	const int integrcols = shape[1]+1;
	_MY_FLOAT_* res_data = static_cast<_MY_FLOAT_*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(cost)));

	const int fill_size = shape[0]*shape[1]*ndisp;
	std::fill_n(res_data,fill_size, RAND_MAX);

#pragma omp parallel num_threads(THREADS_NUM_USED)
{
	_MY_FLOAT_ * slice = new _MY_FLOAT_[integrrows*integrcols];
	const int wc = wsize/2;
	#pragma omp for
	for (int d=0; d<ndisp; d++ ){
		const int dind = d*shape[0]*shape[1];
		std::fill_n(slice,integrrows*integrcols,0);

		for( int i=0; i<shape[0]; i++){
			const int rowind = i*shape[1];
			const int intgrrow = (i+1)*integrcols;
			for(int j=d; j<shape[1]; j++){
				slice[intgrrow+j+1] = fabs( leftp[rowind+j] - rightp[rowind+(j-d)] );
			}
		}

		for( int i=1; i<integrrows; i++ ){

			const int prev_row = (i-1)*integrcols;
			const int intgrrow = i*integrcols;
			for(int j=d; j<integrcols; j++){
				slice[intgrrow+j] += slice[prev_row+j];
			}
		}


		for( int i=0; i<integrrows; i++ ){
			const int rowind = i*integrcols;
			for(int j=d+1; j<integrcols; j++){
				slice[rowind+j] += slice[rowind+j-1];
			}
		}


		for(int i=0; i<shape[0]-wsize;i++){
			const int place_row = (i+wc)*shape[1];
			const int t_row = i*integrcols;
			const int b_row = (i+wsize)*integrcols;

			for(int j=d; j<shape[1]-wsize; j++){

				res_data[dind+place_row+(j+wc)] = slice[b_row+(j+wsize)  ] -
												  slice[b_row+j ] - slice[t_row+(j+wsize) ] +
												  slice[t_row+j];

			}
		}

	}

	delete []  slice;

}

	return cost;



}

//Vectorization Not needed for sad. Compiler can auto vectorize this version!!!

PyObject* zsad(PyObject* left, PyObject *right, int ndisp, int wsize){
    // Cast to pointer to Python Array object
    PyArrayObject* leftA = reinterpret_cast<PyArrayObject*>(left);
    PyArrayObject* rightA = reinterpret_cast<PyArrayObject*>(right);

    //Get the pointer to the data
    const uint8 * leftp = reinterpret_cast<uint8*>(PyArray_DATA(leftA));
    const uint8 * rightp = reinterpret_cast<uint8*>(PyArray_DATA(rightA));

    npy_intp *shape = PyArray_DIMS(leftA);
    npy_intp *costshape = new npy_intp[3];
    costshape[1] = shape[0]; costshape[2] = shape[1]; costshape[0] = ndisp;


    PyObject* cost = PyArray_SimpleNew(3, costshape, NPY_FLOAT32);


	_MY_FLOAT_* res_data = static_cast<_MY_FLOAT_*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(cost)));

	const int fill_size = shape[0]*shape[1]*ndisp;
	std::fill_n(res_data,fill_size, RAND_MAX);

	_MY_FLOAT_* meansl = (_MY_FLOAT_*)calloc( shape[0]*shape[1],sizeof(_MY_FLOAT_) );
	_MY_FLOAT_* meansr = (_MY_FLOAT_*)calloc( shape[0]*shape[1],sizeof(_MY_FLOAT_) );
	const int sqw = wsize*wsize;
	const int wc = wsize/2;

	#pragma omp parallel num_threads(THREADS_NUM_USED)
	{
		#pragma omp for
		for(int i=0; i< shape[0]-wsize; i++){
			for(int j=0; j<shape[1]-wsize;j++){

				for(int wh=0; wh<wsize; wh++){
					for(int ww=0; ww<wsize; ww++){
						meansl[ (i+wc)*shape[1]+(j+wc) ] += leftp[(i+wh)*shape[1]+(j+ww)];
						meansr[ (i+wc)*shape[1]+(j+wc) ] += rightp[(i+wh)*shape[1]+(j+ww)];
					}
				}

				meansl[ (i+wc)*shape[1]+(j+wc) ] /= sqw;
				meansr[ (i+wc)*shape[1]+(j+wc) ] /= sqw;
			}
		}
	}


	#pragma omp parallel num_threads(THREADS_NUM_USED)
	{
		#pragma omp for
		for(int d=0; d<ndisp;d++){
			const int dind = d*shape[0]*shape[1];

			for(int i=0; i<shape[0]-wsize;i++){
				const int place_row = (i+wc)*shape[1];
				for(int j=d; j<shape[1]-wsize; j++){

					res_data[dind+place_row+(j+wc)]=0;
					for(int wh=0; wh<wsize;wh++){
						for(int ww=0; ww<wsize; ww++){

							res_data[dind+place_row+(j+wc)] += fabs( leftp[(i+wh)*shape[1]+(j+ww)] - meansl[ (i+wc)*shape[1]+(j+wc) ] -
																	rightp[(i+wh)*shape[1]+((j-d)+ww)] + meansr[ (i+wc)*shape[1]+((j-d)+wc) ] );
						}
					}
				}
			}
		}
	}
	return cost;
}


PyObject* sobel(PyObject* img ){
    PyArrayObject* imgA = reinterpret_cast<PyArrayObject*>(img);

    //Get the pointer to the data
    uint8 * imgp = reinterpret_cast<uint8*>(PyArray_DATA(imgA));

    npy_intp *shape = PyArray_DIMS(imgA);

    PyObject* sobimg = PyArray_SimpleNew(2, shape, NPY_FLOAT32);

    _MY_FLOAT_ * sobp = static_cast<_MY_FLOAT_*>(PyArray_DATA(reinterpret_cast<PyArrayObject*>(sobimg)));

    std::fill_n(sobp,shape[0]*shape[1],0);

    int * vsobel = (int*)calloc(9,sizeof(int));

    vsobel[0] = -1; vsobel[1] =0; vsobel[2] = 1;
    vsobel[3] = -2; vsobel[4] =0; vsobel[5] = 2;
    vsobel[6] = -1; vsobel[7] =0; vsobel[8] = 1;

#pragma omp parallel num_threads(THREADS_NUM_USED)
    {
		#pragma omp for
    	for(int i=0; i<shape[0]-3; i++){


    		for(int j=0; j<shape[1]-3; j++){

    			_MY_FLOAT_ vsob = vsobel[0]*imgp[i*shape[1]+j] +  vsobel[1]*imgp[i*shape[1]+j+1] + vsobel[2]*imgp[i*shape[1]+j+2] +
    				 	 	vsobel[3]*imgp[(i+1)*shape[1]+j] +  vsobel[4]*imgp[(i+1)*shape[1]+j+1] + vsobel[5]*imgp[(i+1)*shape[1]+j+2]+
    				 	 	vsobel[6]*imgp[(i+2)*shape[1]+j] +  vsobel[7]*imgp[(i+2)*shape[1]+j+1] + vsobel[8]*imgp[(i+2)*shape[1]+j+2];

    			sobp[(i+1)*shape[1]+(j+1)] =  vsob; 
    	}
    }

    }

    return sobimg;
}

int initthreads(){
	int ready=0;
#pragma omp parallel for reduction(+ : ready) num_threads(THREADS_NUM_USED)
	for(int i=0; i<THREADS_NUM_USED; i++){
		ready +=1;
	}
	return ready;
}

BOOST_PYTHON_MODULE(libmatchers) {

	  omp_set_num_threads(THREADS_NUM_USED);
//#if BOOST_VERSION >= 106300 // >= 1.63.0
//		np::initialize();
//#else
//	  np::array::set_module_and_type("numpy", "ndarray");
//#endif
		np::initialize();
    def("census",census);
    def("nccNister",nccNister);
    def("sadsob",sadsob);
    def("zsad",zsad);
    def("sobel",sobel);
	  def("initthreads",initthreads);

    /* Error: return-statement with a value, in function returning 'void' [-fpermissive]
		 *        #define NUMPY_IMPORT_ARRAY_RETVAL NULL
		 * > See solution: https://github.com/numpy/numpy/issues/10486
		 * > 1) Solution1: Okay, so the issue occurs only on py2 + py3c (the initialization function is nonstandard, and has py3 semantics). Solution appears to be to use `import_array1()`;
		 * > 2) Solution2: Or call _import_array(), which allows you more control than just `return`;
		 */ 
    //import_array1(); // work well for python3.7;
		_import_array(); // work well for python3.7;
}
