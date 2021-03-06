#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "minilzo.h"
#include <lzo/lzo1x.h>
/* Ensure we have updated versions 
#if !defined(PY_VERSION_HEX) || (PY_VERSION_HEX < 0x010502f0)
#  error "Need Python version 1.5.2 or greater"
#endif
*/


#undef UNUSED
#define UNUSED(var)     ((void)&var)

static PyObject *LzoError;

#define BLOCK_SIZE        (256*1024l)

#define M_LZO1X_1 1
#define M_LZO1X_1_15 2
#define M_LZO1X_999 3
#  define PyInt_FromLong PyLong_FromLong
#  define _PyString_Resize _PyBytes_Resize

static /* const */ char compress__doc__[] =
"compress one block, the block is splitted in python and should be lower than BLOCK_SIZE\n"
;
static /* const */ char decompress__doc__[] =
"decompress one block, the uncompressed size should be passed as second argument (which is know when parsing lzop structure)\n"
;
static /* const */ char lzo_adler32__doc__[] =
"adler32 checksum.\n"
;


/***********************************************************************
// compress
************************************************************************/
static PyObject *
compress(PyObject *dummy, PyObject *args)
{
    PyObject *result_str;
    lzo_voidp wrkmem = NULL;
    const lzo_bytep in;
    lzo_bytep out;
    lzo_bytep outc;
    lzo_uint in_len;
    lzo_uint out_len;
    lzo_uint new_len;
    int len;
    int level = 1;
    int header = 1;
    int err;

    /* init */
    UNUSED(dummy);
    if (!PyArg_ParseTuple(args, "s#|ii", &in, &len, &level, &header))
        return NULL;
    if (len < 0)
        return NULL;

    in_len = len;
    out_len = in_len + in_len / 16 + 64 + 3;

    /* alloc buffers */
#if PY_MAJOR_VERSION >= 3
    result_str = PyBytes_FromStringAndSize(NULL, 5 + out_len);
#else
    result_str = PyString_FromStringAndSize(NULL, 5 + out_len);
#endif
    if (result_str == NULL)
        return PyErr_NoMemory();
    if (level == 1)
        wrkmem = (lzo_voidp) PyMem_Malloc(LZO1X_1_MEM_COMPRESS);
    else
        wrkmem = (lzo_voidp) PyMem_Malloc(LZO1X_999_MEM_COMPRESS);
    if (wrkmem == NULL)
    {
        Py_DECREF(result_str);
        return PyErr_NoMemory();
    }

    /* compress */
#if PY_MAJOR_VERSION >= 3
    out = (lzo_bytep) PyBytes_AsString(result_str);
#else
    out = (lzo_bytep) PyString_AsString(result_str);
#endif

    Py_BEGIN_ALLOW_THREADS
    outc = header ? out+5 : out; // leave space for header if needed
    new_len = out_len;
    if (level == 1)
    {
        if (header)
            out[0] = 0xf0;
        err = lzo1x_1_compress(in, in_len, outc, &new_len, wrkmem);
    }
    else
    {
        if (header)
            out[0] = 0xf1;
        err = lzo1x_999_compress(in, in_len, outc, &new_len, wrkmem);
    }
    Py_END_ALLOW_THREADS

    PyMem_Free(wrkmem);
    if (err != LZO_E_OK || new_len > out_len)
    {
        /* this should NEVER happen */
        Py_DECREF(result_str);
        PyErr_Format(LzoError, "Error %i while compressing data", err);
        return NULL;
    }

    if (header) {
        /* save uncompressed length */
        out[1] = (unsigned char) ((in_len >> 24) & 0xff);
        out[2] = (unsigned char) ((in_len >> 16) & 0xff);
        out[3] = (unsigned char) ((in_len >>  8) & 0xff);
        out[4] = (unsigned char) ((in_len >>  0) & 0xff);
    }

    /* return */
    if (new_len != out_len)
#if PY_MAJOR_VERSION >= 3
        _PyBytes_Resize(&result_str, header ? new_len + 5 : new_len);
#else
        _PyString_Resize(&result_str, header ? new_len + 5 : new_len);
#endif

    return result_str;
}


/***********************************************************************
// decompress
************************************************************************/

static PyObject *
decompress(PyObject *dummy, PyObject *args)
{
    PyObject *result_str;
    const lzo_bytep in;
    lzo_bytep out;
    lzo_uint in_len;
    lzo_uint out_len;
    lzo_uint new_len;
    int len;
    int buflen = -1;
    int header = 1;
    int err;

    /* init */
    UNUSED(dummy);
    if (!PyArg_ParseTuple(args, "s#|ii", &in, &len, &header, &buflen))
        return NULL;
    if (header) {
        if (len < 5 + 3 || in[0] < 0xf0 || in[0] > 0xf1)
            goto header_error;
        out_len = (in[1] << 24) | (in[2] << 16) | (in[3] << 8) | in[4];
        in_len = len - 5;
        in += 5;
        if ((int)out_len < 0 || in_len > out_len + out_len / 64 + 16 + 3)
            goto header_error;
    }
    else {
        if (buflen < 0) return PyErr_Format(LzoError, "Argument buflen required for headerless decompression");
        out_len = buflen;
        in_len = len;
    }

    /* alloc buffers */
#if PY_MAJOR_VERSION >= 3
    result_str = PyBytes_FromStringAndSize(NULL, out_len);
#else
    result_str = PyString_FromStringAndSize(NULL, out_len);
#endif
    if (result_str == NULL)
        return PyErr_NoMemory();

    /* decompress */
#if PY_MAJOR_VERSION >= 3
    out = (lzo_bytep) PyBytes_AsString(result_str);
#else
    out = (lzo_bytep) PyString_AsString(result_str);
#endif

    Py_BEGIN_ALLOW_THREADS
    new_len = out_len;
    err = lzo1x_decompress_safe(in, in_len, out, &new_len, NULL);
    Py_END_ALLOW_THREADS

    if (err != LZO_E_OK || (header && new_len != out_len) )
    {
        Py_DECREF(result_str);
        PyErr_Format(LzoError, "Compressed data violation %i", err);
        return NULL;
    }

    if (!header && new_len != out_len)
        _PyString_Resize(&result_str, new_len);

    /* success */
    return result_str;

header_error:
    PyErr_SetString(LzoError, "Header error - invalid compressed data");
    return NULL;
}


/***********************************************************************
// optimize
************************************************************************/

static /* const */ char optimize__doc__[] =
"optimize(string[,header[,buflen]]) -- Optimize the representation of the "
"compressed data, returning a string containing the compressed data.\n"
"header - Metadata header is included in input (default: True).\n"
"buflen - If header is False, a buffer length in bytes must be given that "
"will fit the input/output.\n"
;

static PyObject *
optimize(PyObject *dummy, PyObject *args)
{
    PyObject *result_str;
    lzo_bytep in;
    lzo_bytep out;
    lzo_uint in_len;
    lzo_uint out_len;
    lzo_uint new_len;
    int len;
    int err;
    int header = 1;
    int buflen = -1;

    /* init */
    UNUSED(dummy);
    if (!PyArg_ParseTuple(args, "s#|ii", &in, &len, &header, &buflen))
        return NULL;
    if (header) {
        if (len < 5 + 3 || in[0] < 0xf0 || in[0] > 0xf1)
            goto header_error;
        in_len = len - 5;
        out_len = (in[1] << 24) | (in[2] << 16) | (in[3] << 8) | in[4];
        if ((int)out_len < 0 || in_len > out_len + out_len / 64 + 16 + 3)
            goto header_error;
    }
    else {
        if (buflen < 0) return PyErr_Format(LzoError, "Argument buflen required for headerless optimization");
        out_len = buflen;
        in_len = len;
    }

    /* alloc buffers */
#if PY_MAJOR_VERSION >= 3
    result_str = PyBytes_FromStringAndSize(in, len);
#else
    result_str = PyString_FromStringAndSize(in, len);
#endif
    if (result_str == NULL)
        return PyErr_NoMemory();
    out = (lzo_bytep) PyMem_Malloc(out_len > 0 ? out_len : 1);
    if (out == NULL)
    {
        Py_DECREF(result_str);
        return PyErr_NoMemory();
    }

    /* optimize */
#if PY_MAJOR_VERSION >= 3
    in = (lzo_bytep) PyBytes_AsString(result_str);
#else
    in = (lzo_bytep) PyString_AsString(result_str);
#endif

    Py_BEGIN_ALLOW_THREADS
    new_len = out_len;
    err = lzo1x_optimize( header ? in+5 : in, in_len, out, &new_len, NULL);
    Py_END_ALLOW_THREADS

    PyMem_Free(out);
    if (err != LZO_E_OK || (header && new_len != out_len))
    {
        Py_DECREF(result_str);
        PyErr_Format(LzoError, "Compressed data violation %i", err);
        return NULL;
    }

    /* success */
    return result_str;

header_error:
    PyErr_SetString(LzoError, "Header error - invalid compressed data");
    return NULL;
}


/***********************************************************************
// adler32
************************************************************************/

static /* const */ char adler32__doc__[] =
"adler32(string) -- Compute an Adler-32 checksum of string, using "
"a default starting value, and returning an integer value.\n"
"adler32(string, value) -- Compute an Adler-32 checksum of string, using "
"the starting value provided, and returning an integer value\n"
;

static PyObject *
adler32(PyObject *dummy, PyObject *args)
{
    char *buf;
    int len;
    unsigned long val = 1; /* == lzo_adler32(0, NULL, 0); */

    UNUSED(dummy);
    if (!PyArg_ParseTuple(args, "s#|l", &buf, &len, &val))
        return NULL;
    if (len > 0)
    {
        Py_BEGIN_ALLOW_THREADS
        val = lzo_adler32((lzo_uint32)val, (const lzo_bytep)buf, len);
        Py_END_ALLOW_THREADS
    }

#if PY_MAJOR_VERSION >= 3
    return PyLong_FromLong(val);
#else
    return PyInt_FromLong(val);
#endif
}


/***********************************************************************
// crc32
************************************************************************/

static /* const */ char crc32__doc__[] =
"crc32(string) -- Compute a CRC-32 checksum of string, using "
"a default starting value, and returning an integer value.\n"
"crc32(string, value) -- Compute a CRC-32 checksum of string, using "
"the starting value provided, and returning an integer value.\n"
;

static PyObject *
crc32(PyObject *dummy, PyObject *args)
{
    char *buf;
    int len;
    unsigned long val = 0; /* == lzo_crc32(0, NULL, 0); */

    UNUSED(dummy);
    if (!PyArg_ParseTuple(args, "s#|l", &buf, &len, &val))
        return NULL;
    if (len > 0)
        val = lzo_crc32((lzo_uint32)val, (const lzo_bytep)buf, len);
#if PY_MAJOR_VERSION >= 3
    return PyLong_FromLong(val);
#else
    return PyInt_FromLong(val);
#endif
}


//static /* const */ PyMethodDef methods[] =
//{
//   {"adler32",    (PyCFunction)adler32,    METH_VARARGS, adler32__doc__},
//    {"compress",   (PyCFunction)compress,   METH_VARARGS, compress__doc__},
//    {"crc32",      (PyCFunction)crc32,      METH_VARARGS, crc32__doc__},
//    {"decompress", (PyCFunction)decompress, METH_VARARGS, decompress__doc__},
//    {"optimize",   (PyCFunction)optimize,   METH_VARARGS, optimize__doc__},
//、    {NULL, NULL, 0, NULL}
//};

static PyObject *
compress_block(PyObject *dummy, PyObject *args)
{
  PyObject *result;
  lzo_voidp wrkmem = NULL;

  const lzo_bytep in;
  Py_ssize_t in_len;

  lzo_bytep out;
  Py_ssize_t out_len;
  Py_ssize_t new_len;

  lzo_uint32_t wrk_len;

  int level;
  int method;
  int err;
  UNUSED(dummy);

  if (!PyArg_ParseTuple(args, "s#II", &in, &in_len, &method, &level))
    return NULL;

  out_len = in_len + in_len / 64 + 16 + 3;

  result = PyBytes_FromStringAndSize(NULL, out_len);

  if (result == NULL){
    return PyErr_NoMemory();
  }

  if (method == M_LZO1X_1)
      wrk_len = LZO1X_1_MEM_COMPRESS;
#ifdef USE_LIBLZO
  else if (method == M_LZO1X_1_15)
      wrk_len = LZO1X_1_15_MEM_COMPRESS;
  else if (method == M_LZO1X_999)
      wrk_len = LZO1X_999_MEM_COMPRESS;
#endif


  assert(wrk_len <= WRK_LEN);

  wrkmem = (lzo_voidp) PyMem_Malloc(wrk_len);
  out = (lzo_bytep) PyBytes_AsString(result);
  
  if (method == M_LZO1X_1){
    err = lzo1x_1_compress(in, (lzo_uint) in_len, out, (lzo_uint*) &new_len, wrkmem);
  }
#ifdef USE_LIBLZO
  else if (method == M_LZO1X_1_15){
    err = lzo1x_1_15_compress(in, (lzo_uint) in_len,
                                    out, (lzo_uint*) &new_len, wrkmem);
  }
  else if (method == M_LZO1X_999){
    err = lzo1x_999_compress_level(in, (lzo_uint)in_len,
                                         out, (lzo_uint*) &new_len, wrkmem,
                                         NULL, 0, 0, level);
  }
#endif
  else{
    PyMem_Free(wrkmem);
    Py_DECREF(result);
    PyErr_SetString(LzoError, "Compression method not supported");
    return NULL;
  }

  PyMem_Free(wrkmem);
  if (err != LZO_E_OK || new_len > out_len)
  {
    /* this should NEVER happen */
    Py_DECREF(result);
    PyErr_Format(LzoError, "Error %i while compressing data", err);
    return NULL;
  }

  if (new_len != out_len)
    _PyBytes_Resize(&result, new_len);
    /* 
      If the reallocation fails, the result is set to NULL, and memory exception is set
      So should raise right exception to python environment without additional check
    */

  return result;

}

static PyObject *
decompress_block(PyObject *dummy, PyObject *args)
{
  PyObject *result;

  lzo_bytep out;
  lzo_bytep in;

  Py_ssize_t in_len;
  Py_ssize_t dst_len;
  Py_ssize_t len;

  int err;
  UNUSED(dummy);
  
  if (!PyArg_ParseTuple(args, "s#n", &in, &in_len, &dst_len))
    return NULL;

  result = PyBytes_FromStringAndSize(NULL, dst_len);

  if (result == NULL) {
    return PyErr_NoMemory();
  }

  out = (lzo_bytep) PyBytes_AS_STRING(result);

  len = dst_len;
  err = lzo1x_decompress_safe(in, (lzo_uint)in_len, out, (lzo_uint*)&len, NULL);

  if (err != LZO_E_OK){
      Py_DECREF(result);
      result = NULL;
      PyErr_SetString(LzoError, "internal error - decompression failed");
      return NULL;
  }
  if (len != dst_len){
    return NULL;
  }

  return result;

}

static PyObject *
py_lzo_adler32(PyObject *dummy, PyObject *args)
{
  lzo_uint32 value = 1;
  const lzo_bytep in;
  Py_ssize_t len;

  lzo_uint32 new;

  if (!PyArg_ParseTuple(args, "s#|I", &in, &len, &value))
    return NULL;

  if(len>0){
    new = lzo_adler32(value, in, len);
    return Py_BuildValue("I", new);
  }
  else{
    return Py_BuildValue("I", value);
  }
}

#ifdef USE_LIBLZO
static PyObject *
py_lzo_crc32(PyObject *dummy, PyObject *args)
{
  lzo_uint32 value;
  const lzo_bytep in;
  Py_ssize_t len;

  lzo_uint32 new;

  if (!PyArg_ParseTuple(args, "Is#", &value, &in, &len))
    return NULL;
  
  if(len>0){
    new = lzo_crc32(value, in, len);
    return Py_BuildValue("I", new);
  }
  else{
    return Py_BuildValue("I", value);
  }
}
#endif

/***********************************************************************
// main
************************************************************************/

static /* const */ PyMethodDef methods[] =
{
    {"compress_block", (PyCFunction)compress_block, METH_VARARGS, compress__doc__},
    {"decompress_block", (PyCFunction)decompress_block, METH_VARARGS, decompress__doc__},
    {"lzo_adler32", (PyCFunction)py_lzo_adler32, METH_VARARGS, lzo_adler32__doc__},
#ifdef USE_LIBLZO
    {"lzo_crc32", (PyCFunction)py_lzo_crc32, METH_VARARGS, decompress__doc__},
#endif
    {NULL, NULL, 0, NULL}
};

static PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "lzo", /* name */
    "Python bindings for the LZO data compression library", /* doc */
    -1, /* size */
    methods, /* methods */
    NULL, /* reload */
    NULL, /* traverse */
    NULL, /* clear */
    NULL, /* free */
};


static /* const */ char module_documentation[]=
"This is a python library deals with lzo files compressed with lzop.\n\n"

;


#ifdef _MSC_VER
_declspec(dllexport)
#endif
PyMODINIT_FUNC PyInit__lzo(void)
//void init_lzo(void)
{
    PyObject *m, *d, *v;
    //if (lzo_init() != LZO_E_OK)
   // {
    //    return;
    //}

    m = PyModule_Create(&module);
    d = PyModule_GetDict(m);

    LzoError = PyErr_NewException("__lzo.error", NULL, NULL);
    PyDict_SetItemString(d, "error", LzoError);

    v = PyBytes_FromString("<iridiummx@gmail.com>");
    PyDict_SetItemString(d, "__author__", v);
    Py_DECREF(v);

    v = PyLong_FromLong(LZO_VERSION);
    PyDict_SetItemString(d, "LZO_VERSION", v);
    Py_DECREF(v);
    v = PyBytes_FromString(LZO_VERSION_STRING);
    PyDict_SetItemString(d, "LZO_VERSION_STRING", v);
    Py_DECREF(v);
    v = PyBytes_FromString(LZO_VERSION_DATE);
    PyDict_SetItemString(d, "LZO_VERSION_DATE", v);
    Py_DECREF(v);
    return m;
}


/*
vi:ts=4:et
*/
