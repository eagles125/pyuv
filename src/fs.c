
/* If true, st_?time is float */
static int _stat_float_times = 1;

static PyObject*
stat_float_times(PyObject* self, PyObject *args)
{
    int newval = -1;
    if (!PyArg_ParseTuple(args, "|i:stat_float_times", &newval)) {
        return NULL;
    }
    if (newval == -1) {
        /* Return old value */
        return PyBool_FromLong(_stat_float_times);
    }
    _stat_float_times = newval;
    Py_RETURN_NONE;
}


static PyObject *
format_time(uv_timespec_t tspec)
{
    if (_stat_float_times) {
        return PyFloat_FromDouble(tspec.tv_sec + 1e-9*tspec.tv_nsec);
    } else {
        return PyInt_FromLong(tspec.tv_sec);
    }
}


static void
stat_to_pyobj(uv_stat_t *st, PyObject **stat_data) {
    PyStructSequence_SET_ITEM(*stat_data, 0, PyLong_FromUnsignedLongLong(st->st_mode));
    PyStructSequence_SET_ITEM(*stat_data, 1, PyLong_FromUnsignedLongLong(st->st_ino));
    PyStructSequence_SET_ITEM(*stat_data, 2, PyLong_FromUnsignedLongLong(st->st_dev));
    PyStructSequence_SET_ITEM(*stat_data, 3, PyLong_FromUnsignedLongLong(st->st_nlink));
    PyStructSequence_SET_ITEM(*stat_data, 4, PyLong_FromUnsignedLongLong(st->st_uid));
    PyStructSequence_SET_ITEM(*stat_data, 5, PyLong_FromUnsignedLongLong(st->st_gid));
    PyStructSequence_SET_ITEM(*stat_data, 6, PyLong_FromUnsignedLongLong(st->st_size));
    PyStructSequence_SET_ITEM(*stat_data, 7, format_time(st->st_atim));
    PyStructSequence_SET_ITEM(*stat_data, 8, format_time(st->st_mtim));
    PyStructSequence_SET_ITEM(*stat_data, 9, format_time(st->st_ctim));
    PyStructSequence_SET_ITEM(*stat_data, 10, PyLong_FromUnsignedLongLong(st->st_blksize));
    PyStructSequence_SET_ITEM(*stat_data, 11, PyLong_FromUnsignedLongLong(st->st_blocks));
    PyStructSequence_SET_ITEM(*stat_data, 12, PyLong_FromUnsignedLongLong(st->st_rdev));
    /* not supported by libuv yet */
    Py_INCREF(Py_None);
    PyStructSequence_SET_ITEM(*stat_data, 13, Py_None);
    Py_INCREF(Py_None);
    PyStructSequence_SET_ITEM(*stat_data, 14, Py_None);
    Py_INCREF(Py_None);
    PyStructSequence_SET_ITEM(*stat_data, 15, Py_None);
}


static void
process_stat(uv_fs_t* req, PyObject **path, PyObject **stat_data, PyObject **errorno) {
    uv_stat_t *st;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_STAT || req->fs_type == UV_FS_LSTAT || req->fs_type == UV_FS_FSTAT);

    st = &req->statbuf;

    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
        *stat_data = Py_None;
        Py_INCREF(Py_None);
        return;
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
    }

    *stat_data = PyStructSequence_New(&StatResultType);
    if (!*stat_data) {
        PyErr_Clear();
        Py_DECREF(*path);
        Py_DECREF(*errorno);
        *path = Py_None;
        Py_INCREF(Py_None);
        *errorno = PyInt_FromLong((long)UV_ENOMEM);
        *stat_data = Py_None;
        Py_INCREF(Py_None);
        return;
    }
    stat_to_pyobj(st, stat_data);

}


static void
stat_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *stat_data, *path;

    process_stat(req, &path, &stat_data, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, stat_data, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(stat_data);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
unlink_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_UNLINK);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
mkdir_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_MKDIR);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
rmdir_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_RMDIR);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
rename_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_RENAME);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
chmod_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_CHMOD || req->fs_type == UV_FS_FCHMOD);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
link_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_LINK);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
symlink_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_SYMLINK);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_readlink(uv_fs_t* req, PyObject **path, PyObject **errorno)
{
    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_READLINK);

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
        *path = Py_None;
        Py_INCREF(Py_None);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
        *path = Py_BuildValue("s", req->ptr);
    }
}

static void
readlink_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    process_readlink(req, &path, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
chown_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_CHOWN || req->fs_type == UV_FS_FCHOWN);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_open(uv_fs_t* req, PyObject **path, PyObject **fd, PyObject **errorno)
{
    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_OPEN);

    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
        *fd = Py_None;
        Py_INCREF(Py_None);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
        *fd = PyInt_FromLong((long)req->result);
    }
}

static void
open_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *fd, *errorno, *path;

    process_open(req, &path, &fd, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, fd, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(fd);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
close_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_CLOSE);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);
    Py_DECREF(loop);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_read(uv_fs_t* req, PyObject **path, PyObject **read_data, PyObject **errorno)
{
    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_READ);

    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
        *read_data = Py_None;
        Py_INCREF(Py_None);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
        *read_data = PyBytes_FromStringAndSize(req->buf, req->result);
    }
}

static void
read_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *read_data, *path;

    process_read(req, &path, &read_data, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, read_data, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(read_data);
    Py_DECREF(path);
    Py_DECREF(errorno);

    PyMem_Free(req->buf);
    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_write(uv_fs_t* req, PyObject **path, PyObject **bytes_written, PyObject **errorno)
{
    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_WRITE);

    *bytes_written = PyInt_FromLong((long)req->result);
    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
    }
}

static void
write_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *bytes_written, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_WRITE);

    process_write(req, &path, &bytes_written, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, bytes_written, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(bytes_written);
    Py_DECREF(path);
    Py_DECREF(errorno);

    PyMem_Free(req->buf);
    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
fsync_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_FSYNC || req->fs_type == UV_FS_FDATASYNC);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
ftruncate_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_FTRUNCATE);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_readdir(uv_fs_t* req, PyObject **path, PyObject **files, PyObject **errorno)
{
    int r;
    char *ptr;
    PyObject *item;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_READDIR);

    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
        *files = Py_None;
        Py_INCREF(Py_None);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
        *files = PyList_New(0);
        if (!files) {
            PyErr_Clear();
            *files = Py_None;
            Py_INCREF(Py_None);
        } else {
            r = req->result;
            ptr = req->ptr;
            while (r--) {
                item = Py_BuildValue("s", ptr);
                PyList_Append(*files, item);
                Py_DECREF(item);
                ptr += strlen(ptr) + 1;
            }
        }
    }

}

static void
readdir_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *files, *path;

    process_readdir(req, &path, &files, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, files, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(files);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
process_sendfile(uv_fs_t* req, PyObject **path, PyObject **bytes_written, PyObject **errorno)
{
    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_SENDFILE);

    *bytes_written = PyInt_FromLong((long)req->result);

    if (req->path != NULL) {
        *path = Py_BuildValue("s", req->path);
    } else {
        *path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        *errorno = PyInt_FromLong((long)req->errorno);
    } else {
        *errorno = Py_None;
        Py_INCREF(Py_None);
    }
}

static void
sendfile_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *bytes_written, *path;

    process_sendfile(req, &path, &bytes_written, &errorno);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, bytes_written, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(bytes_written);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static void
utime_cb(uv_fs_t* req) {
    PyGILState_STATE gstate = PyGILState_Ensure();
    Loop *loop;
    FSRequest *fs_req;
    PyObject *result, *errorno, *path;

    ASSERT(req);
    ASSERT(req->fs_type == UV_FS_UTIME || req->fs_type == UV_FS_FUTIME);

    fs_req = PYUV_CONTAINER_OF(req, FSRequest, req);
    loop = REQUEST(fs_req)->loop;

    if (req->path != NULL) {
        path = Py_BuildValue("s", req->path);
    } else {
        path = Py_None;
        Py_INCREF(Py_None);
    }

    if (req->errorno != 0) {
        errorno = PyInt_FromLong((long)req->errorno);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    result = PyObject_CallFunctionObjArgs(fs_req->callback, loop, path, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(loop);
    }

    Py_XDECREF(result);
    Py_DECREF(path);
    Py_DECREF(errorno);

    uv_fs_req_cleanup(req);
    UV_REQUEST(fs_req) = NULL;
    Py_DECREF(fs_req);

    PyGILState_Release(gstate);
}


static PyObject *
stat_func(PyObject *args, PyObject *kwargs, int type)
{
    int r;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *stat_data, *py_errorno, *ret;

    static char *kwlist[] = {"loop", "path", "callback", NULL};

    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!s|O:stat", kwlist, &LoopType, &loop, &path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    if (type == UV_FS_STAT) {
        r = uv_fs_stat(loop->uv_loop, &fs_req->req, path, (callback != Py_None) ? stat_cb : NULL);
    } else {
        r = uv_fs_lstat(loop->uv_loop, &fs_req->req, path, (callback != Py_None) ? stat_cb : NULL);
    }
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_stat(&fs_req->req, &py_path, &stat_data, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = stat_data;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_stat(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    UNUSED_ARG(obj);
    return stat_func(args, kwargs, UV_FS_STAT);
}


static PyObject *
FS_func_lstat(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    UNUSED_ARG(obj);
    return stat_func(args, kwargs, UV_FS_LSTAT);
}


static PyObject *
FS_func_fstat(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *stat_data, *py_errorno, *ret;

    static char *kwlist[] = {"loop", "fd", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!l|O:fstat", kwlist, &LoopType, &loop, &fd, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_fstat(loop->uv_loop, &fs_req->req, (uv_file)fd, (callback != Py_None) ? stat_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_stat(&fs_req->req, &py_path, &stat_data, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = stat_data;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_unlink(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!s|O:unlink", kwlist, &LoopType, &loop, &path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_unlink(loop->uv_loop, &fs_req->req, path, (callback != Py_None) ? unlink_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_mkdir(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, mode;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "mode", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!si|O:mkdir", kwlist, &LoopType, &loop, &path, &mode, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_mkdir(loop->uv_loop, &fs_req->req, path, mode, (callback != Py_None) ? mkdir_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_rmdir(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!s|O:rmdir", kwlist, &LoopType, &loop, &path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_rmdir(loop->uv_loop, &fs_req->req, path, (callback != Py_None) ? rmdir_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_rename(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path, *new_path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "new_path", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!ss|O:rename", kwlist, &LoopType, &loop, &path, &new_path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_rename(loop->uv_loop, &fs_req->req, path, new_path, (callback != Py_None) ? rename_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_chmod(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, mode;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "mode", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!si|O:chmod", kwlist, &LoopType, &loop, &path, &mode, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_chmod(loop->uv_loop, &fs_req->req, path, mode, (callback != Py_None) ? chmod_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_fchmod(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, mode;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "mode", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!li|O:fchmod", kwlist, &LoopType, &loop, &fd, &mode, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_fchmod(loop->uv_loop, &fs_req->req, (uv_file)fd, mode, (callback != Py_None) ? chmod_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_link(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path, *new_path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "new_path", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!ss|O:link", kwlist, &LoopType, &loop, &path, &new_path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_link(loop->uv_loop, &fs_req->req, path, new_path, (callback != Py_None) ? link_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_symlink(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, flags;
    char *path, *new_path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "new_path", "flags", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!ssi|O:symlink", kwlist, &LoopType, &loop, &path, &new_path, &flags, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_symlink(loop->uv_loop, &fs_req->req, path, new_path, flags, (callback != Py_None) ? symlink_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_readlink(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *py_errorno, *ret;

    static char *kwlist[] = {"loop", "path", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!s|O:readlink", kwlist, &LoopType, &loop, &path, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_readlink(loop->uv_loop, &fs_req->req, path, (callback != Py_None) ? readlink_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_readlink(&fs_req->req, &py_path, &py_errorno);
        Py_DECREF(py_errorno);
        ret = py_path;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_chown(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, uid, gid;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "uid", "gid", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!sii|O:chown", kwlist, &LoopType, &loop, &path, &uid, &gid, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_chown(loop->uv_loop, &fs_req->req, path, uid, gid, (callback != Py_None) ? chown_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_fchown(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, uid, gid;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "uid", "gid", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!lii|O:fchown", kwlist, &LoopType, &loop, &fd, &uid, &gid, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_fchown(loop->uv_loop, &fs_req->req, (uv_file)fd, uid, gid, (callback != Py_None) ? chown_cb : NULL);
    if (r != 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_open(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, flags, mode;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *py_errorno, *fd, *ret;

    static char *kwlist[] = {"loop", "path", "flags", "mode", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!sii|O:open", kwlist, &LoopType, &loop, &path, &flags, &mode, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_open(loop->uv_loop, &fs_req->req, path, flags, mode, (callback != Py_None) ? open_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_open(&fs_req->req, &py_path, &fd, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = fd;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_close(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!l|O:close", kwlist, &LoopType, &loop, &fd, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_close(loop->uv_loop, &fs_req->req, (uv_file)fd, (callback != Py_None) ? close_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_read(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, length, offset;
    long fd;
    char *buf;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *py_errorno, *read_data, *ret;

    static char *kwlist[] = {"loop", "fd", "length", "offset", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    buf = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!lii|O:read", kwlist, &LoopType, &loop, &fd, &length, &offset, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    buf = PyMem_Malloc(length);
    if (!buf) {
        PyErr_NoMemory();
        ret = NULL;
        goto end;
    }

    r = uv_fs_read(loop->uv_loop, &fs_req->req, (uv_file)fd, buf, length, offset, (callback != Py_None) ? read_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_read(&fs_req->req, &py_path, &read_data, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = read_data;
    }

end:
    PyMem_Free(buf);
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_write(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, offset;
    long fd;
    char *pbuf, *buf;
    Loop *loop;
    FSRequest *fs_req;
    Py_ssize_t length;
    PyObject *callback, *py_path, *py_errorno, *written_bytes, *ret;

    static char *kwlist[] = {"loop", "fd", "write_data", "offset", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    buf = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!ls#i|O:write", kwlist, &LoopType, &loop, &fd, &pbuf, &length, &offset, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    buf = PyMem_Malloc(length);
    if (!buf) {
        PyErr_NoMemory();
        ret = NULL;
        goto end;
    }
    memcpy(buf, pbuf, length);

    r = uv_fs_write(loop->uv_loop, &fs_req->req, (uv_file)fd, buf, length, offset, (callback != Py_None) ? write_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_write(&fs_req->req, &py_path, &written_bytes, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = written_bytes;
    }

end:
    PyMem_Free(buf);
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_fsync(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!l|O:fsync", kwlist, &LoopType, &loop, &fd, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_fsync(loop->uv_loop, &fs_req->req, (uv_file)fd, (callback != Py_None) ? fsync_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_fdatasync(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!l|O:fdatasync", kwlist, &LoopType, &loop, &fd, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_fdatasync(loop->uv_loop, &fs_req->req, (uv_file)fd, (callback != Py_None) ? fsync_cb : NULL);
    if (r != 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_ftruncate(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, offset;
    long fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "offset", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!li|O:ftruncate", kwlist, &LoopType, &loop, &fd, &offset, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_ftruncate(loop->uv_loop, &fs_req->req, (uv_file)fd, offset, (callback != Py_None) ? ftruncate_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_readdir(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, flags;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *py_errorno, *files, *ret;

    static char *kwlist[] = {"loop", "path", "flags", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!si|O:readdir", kwlist, &LoopType, &loop, &path, &flags, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_readdir(loop->uv_loop, &fs_req->req, path, flags, (callback != Py_None) ? readdir_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_readdir(&fs_req->req, &py_path, &files, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = files;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_sendfile(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r, in_offset, length;
    long out_fd, in_fd;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback, *py_path, *py_errorno, *bytes_written, *ret;

    static char *kwlist[] = {"loop", "out_fd", "in_fd", "in_offset", "length", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!llii|O:sendfile", kwlist, &LoopType, &loop, &out_fd, &in_fd, &in_offset, &length, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_sendfile(loop->uv_loop, &fs_req->req, (uv_file)out_fd, (uv_file)in_fd, in_offset, length, (callback != Py_None) ? sendfile_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        ret = NULL;
        goto end;
    }

    if (callback != Py_None) {
        /* No need to cleanup, it will be done in the callback */
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        process_sendfile(&fs_req->req, &py_path, &bytes_written, &py_errorno);
        Py_DECREF(py_path);
        Py_DECREF(py_errorno);
        ret = bytes_written;
    }

end:
    Py_DECREF(fs_req);
    return ret;
}


static PyObject *
FS_func_utime(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    double atime, mtime;
    char *path;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "path", "atime", "mtime", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!sdd|O:utime", kwlist, &LoopType, &loop, &path, &atime, &mtime, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_utime(loop->uv_loop, &fs_req->req, path, atime, mtime, (callback != Py_None) ? utime_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyObject *
FS_func_futime(PyObject *obj, PyObject *args, PyObject *kwargs)
{
    int r;
    long fd;
    double atime, mtime;
    Loop *loop;
    FSRequest *fs_req;
    PyObject *callback;

    static char *kwlist[] = {"loop", "fd", "atime", "mtime", "callback", NULL};

    UNUSED_ARG(obj);
    fs_req = NULL;
    callback = Py_None;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!ldd|O:futime", kwlist, &LoopType, &loop, &fd, &atime, &mtime, &callback)) {
        return NULL;
    }

    if (callback != Py_None && !PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    fs_req = (FSRequest *)PyObject_CallFunctionObjArgs((PyObject *)&FSRequestType, loop, callback, NULL);
    if (!fs_req) {
        return NULL;
    }

    r = uv_fs_futime(loop->uv_loop, &fs_req->req, (uv_file)fd, atime, mtime, (callback != Py_None) ? utime_cb : NULL);
    if (r < 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSError);
        goto end;
    }

    if (callback != Py_None) {
        Py_INCREF(fs_req);
        return (PyObject *)fs_req;
    } else {
        Py_RETURN_NONE;
    }

end:
    Py_DECREF(fs_req);
    return NULL;
}


static PyMethodDef
FS_methods[] = {
    { "stat", (PyCFunction)FS_func_stat, METH_VARARGS|METH_KEYWORDS, "stat" },
    { "lstat", (PyCFunction)FS_func_lstat, METH_VARARGS|METH_KEYWORDS, "lstat" },
    { "fstat", (PyCFunction)FS_func_fstat, METH_VARARGS|METH_KEYWORDS, "fstat" },
    { "unlink", (PyCFunction)FS_func_unlink, METH_VARARGS|METH_KEYWORDS, "Remove a file from the filesystem." },
    { "mkdir", (PyCFunction)FS_func_mkdir, METH_VARARGS|METH_KEYWORDS, "Create a directory." },
    { "rmdir", (PyCFunction)FS_func_rmdir, METH_VARARGS|METH_KEYWORDS, "Remove a directory." },
    { "rename", (PyCFunction)FS_func_rename, METH_VARARGS|METH_KEYWORDS, "Rename a file." },
    { "chmod", (PyCFunction)FS_func_chmod, METH_VARARGS|METH_KEYWORDS, "Change file permissions." },
    { "fchmod", (PyCFunction)FS_func_fchmod, METH_VARARGS|METH_KEYWORDS, "Change file permissions." },
    { "link", (PyCFunction)FS_func_link, METH_VARARGS|METH_KEYWORDS, "Create hardlink." },
    { "symlink", (PyCFunction)FS_func_symlink, METH_VARARGS|METH_KEYWORDS, "Create symbolic link." },
    { "readlink", (PyCFunction)FS_func_readlink, METH_VARARGS|METH_KEYWORDS, "Get the path to which the symbolic link points." },
    { "chown", (PyCFunction)FS_func_chown, METH_VARARGS|METH_KEYWORDS, "Change file ownership." },
    { "fchown", (PyCFunction)FS_func_fchown, METH_VARARGS|METH_KEYWORDS, "Change file ownership." },
    { "open", (PyCFunction)FS_func_open, METH_VARARGS|METH_KEYWORDS, "Open file." },
    { "close", (PyCFunction)FS_func_close, METH_VARARGS|METH_KEYWORDS, "Close file." },
    { "read", (PyCFunction)FS_func_read, METH_VARARGS|METH_KEYWORDS, "Read data from a file." },
    { "write", (PyCFunction)FS_func_write, METH_VARARGS|METH_KEYWORDS, "Write data to a file." },
    { "fsync", (PyCFunction)FS_func_fsync, METH_VARARGS|METH_KEYWORDS, "Sync all changes made to a file." },
    { "fdatasync", (PyCFunction)FS_func_fdatasync, METH_VARARGS|METH_KEYWORDS, "Sync data changes made to a file." },
    { "ftruncate", (PyCFunction)FS_func_ftruncate, METH_VARARGS|METH_KEYWORDS, "Truncate the contents of a file to the specified offset." },
    { "readdir", (PyCFunction)FS_func_readdir, METH_VARARGS|METH_KEYWORDS, "List files from a directory." },
    { "sendfile", (PyCFunction)FS_func_sendfile, METH_VARARGS|METH_KEYWORDS, "Sends a regular file to a stream socket." },
    { "utime", (PyCFunction)FS_func_utime, METH_VARARGS|METH_KEYWORDS, "Update file times." },
    { "futime", (PyCFunction)FS_func_futime, METH_VARARGS|METH_KEYWORDS, "Update file times." },
    { "stat_float_times", (PyCFunction)stat_float_times, METH_VARARGS, "Use floats for times in stat structs." },
    { NULL }
};


/* FSEvent handle */

static void
on_fsevent_callback(uv_fs_event_t *handle, const char *filename, int events, int status)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    FSEvent *self;
    PyObject *result, *py_filename, *py_events, *errorno;

    ASSERT(handle);

    self = PYUV_CONTAINER_OF(handle, FSEvent, fsevent_h);

    /* Object could go out of scope in the callback, increase refcount to avoid it */
    Py_INCREF(self);

    if (filename) {
        py_filename = Py_BuildValue("s", filename);
    } else {
        py_filename = Py_None;
        Py_INCREF(Py_None);
    }

    if (status < 0) {
        uv_err_t err = uv_last_error(UV_HANDLE_LOOP(self));
        errorno = PyInt_FromLong((long)err.code);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
    }

    py_events = PyInt_FromLong((long)events);

    result = PyObject_CallFunctionObjArgs(self->callback, self, py_filename, py_events, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(HANDLE(self)->loop);
    }
    Py_XDECREF(result);
    Py_DECREF(py_events);
    Py_DECREF(py_filename);
    Py_DECREF(errorno);

    Py_DECREF(self);
    PyGILState_Release(gstate);
}


static PyObject *
FSEvent_filename_get(FSEvent *self, void *closure)
{
    UNUSED_ARG(closure);

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);

    return Py_BuildValue("s", self->fsevent_h.filename);
}


static int
FSEvent_tp_init(FSEvent *self, PyObject *args, PyObject *kwargs)
{
    int r, flags;
    char *path;
    Loop *loop;
    PyObject *callback, *tmp;

    static char *kwlist[] = {"loop", "path", "flags", "callback", NULL};

    UNUSED_ARG(kwargs);

    RAISE_IF_HANDLE_INITIALIZED(self, -1);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!siO:__init__", kwlist, &LoopType, &loop, &path, &flags, &callback)) {
        return -1;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return -1;
    }

    r = uv_fs_event_init(loop->uv_loop, &self->fsevent_h, path, on_fsevent_callback, flags);
    if (r != 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSEventError);
        return -1;
    }

    tmp = self->callback;
    Py_INCREF(callback);
    self->callback = callback;
    Py_XDECREF(tmp);

    initialize_handle(HANDLE(self), loop);

    return 0;
}


static PyObject *
FSEvent_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    FSEvent *self;

    self = (FSEvent *)HandleType.tp_new(type, args, kwargs);
    if (!self) {
        return NULL;
    }

    self->fsevent_h.data = self;
    UV_HANDLE(self) = (uv_handle_t *)&self->fsevent_h;

    return (PyObject *)self;
}


static int
FSEvent_tp_traverse(FSEvent *self, visitproc visit, void *arg)
{
    Py_VISIT(self->callback);
    return HandleType.tp_traverse((PyObject *)self, visit, arg);
}


static int
FSEvent_tp_clear(FSEvent *self)
{
    Py_CLEAR(self->callback);
    return HandleType.tp_clear((PyObject *)self);
}


static PyGetSetDef FSEvent_tp_getsets[] = {
    {"filename", (getter)FSEvent_filename_get, NULL, "Name of the file/directory being monitored.", NULL},
    {NULL}
};


static PyTypeObject FSEventType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.fs.FSEvent",                                              /*tp_name*/
    sizeof(FSEvent),                                                /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    0,                                                              /*tp_dealloc*/
    0,                                                              /*tp_print*/
    0,                                                              /*tp_getattr*/
    0,                                                              /*tp_setattr*/
    0,                                                              /*tp_compare*/
    0,                                                              /*tp_repr*/
    0,                                                              /*tp_as_number*/
    0,                                                              /*tp_as_sequence*/
    0,                                                              /*tp_as_mapping*/
    0,                                                              /*tp_hash */
    0,                                                              /*tp_call*/
    0,                                                              /*tp_str*/
    0,                                                              /*tp_getattro*/
    0,                                                              /*tp_setattro*/
    0,                                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,  /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)FSEvent_tp_traverse,                              /*tp_traverse*/
    (inquiry)FSEvent_tp_clear,                                      /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    0,                                                              /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    0,                                                              /*tp_methods*/
    0,                                                              /*tp_members*/
    FSEvent_tp_getsets,                                             /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    0,                                                              /*tp_dictoffset*/
    (initproc)FSEvent_tp_init,                                      /*tp_init*/
    0,                                                              /*tp_alloc*/
    FSEvent_tp_new,                                                 /*tp_new*/
};


/* FSPoll handle */

static void
on_fspoll_callback(uv_fs_poll_t *handle, int status, const uv_stat_t *prev, const uv_stat_t *curr)
{
    PyGILState_STATE gstate = PyGILState_Ensure();
    FSPoll *self;
    PyObject *result, *errorno, *prev_stat_data, *curr_stat_data;

    ASSERT(handle);

    self = PYUV_CONTAINER_OF(handle, FSPoll, fspoll_h);

    /* Object could go out of scope in the callback, increase refcount to avoid it */
    Py_INCREF(self);

    if (status < 0) {
        uv_err_t err = uv_last_error(UV_HANDLE_LOOP(self));
        errorno = PyInt_FromLong((long)err.code);
        prev_stat_data = Py_None;
        curr_stat_data = Py_None;
        Py_INCREF(Py_None);
        Py_INCREF(Py_None);
    } else {
        errorno = Py_None;
        Py_INCREF(Py_None);
        prev_stat_data = PyStructSequence_New(&StatResultType);
        if (!prev_stat_data) {
            PyErr_Clear();
            prev_stat_data = Py_None;
            Py_INCREF(Py_None);
        } else {
            stat_to_pyobj((uv_stat_t *)prev, &prev_stat_data);
        }
        curr_stat_data = PyStructSequence_New(&StatResultType);
        if (!curr_stat_data) {
            PyErr_Clear();
            curr_stat_data = Py_None;
            Py_INCREF(Py_None);
        } else {
            stat_to_pyobj((uv_stat_t *)curr, &curr_stat_data);
        }
    }

    result = PyObject_CallFunctionObjArgs(self->callback, self, prev_stat_data, curr_stat_data, errorno, NULL);
    if (result == NULL) {
        handle_uncaught_exception(HANDLE(self)->loop);
    }
    Py_XDECREF(result);

    Py_DECREF(self);
    PyGILState_Release(gstate);
}


static PyObject *
FSPoll_func_start(FSPoll *self, PyObject *args, PyObject *kwargs)
{
    int r;
    char *path;
    double interval;
    PyObject *tmp, *callback;

    static char *kwlist[] = {"path", "interval", "callback", NULL};

    tmp = NULL;

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);
    RAISE_IF_HANDLE_CLOSED(self, PyExc_HandleClosedError, NULL);

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sdO:start", kwlist, &path, &interval, &callback)) {
        return NULL;
    }

    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "a callable is required");
        return NULL;
    }

    if (interval < 0.0) {
        PyErr_SetString(PyExc_ValueError, "a positive value or zero is required");
        return NULL;
    }

    r = uv_fs_poll_start(&self->fspoll_h, on_fspoll_callback, path, (unsigned int)interval*1000);
    if (r != 0) {
        RAISE_UV_EXCEPTION(UV_HANDLE_LOOP(self), PyExc_FSPollError);
        return NULL;
    }

    tmp = self->callback;
    Py_INCREF(callback);
    self->callback = callback;
    Py_XDECREF(tmp);

    Py_RETURN_NONE;
}


static PyObject *
FSPoll_func_stop(FSPoll *self)
{
    int r;

    RAISE_IF_HANDLE_NOT_INITIALIZED(self, NULL);
    RAISE_IF_HANDLE_CLOSED(self, PyExc_HandleClosedError, NULL);

    r = uv_fs_poll_stop(&self->fspoll_h);
    if (r != 0) {
        RAISE_UV_EXCEPTION(UV_HANDLE_LOOP(self), PyExc_FSPollError);
        return NULL;
    }

    Py_XDECREF(self->callback);
    self->callback = NULL;

    Py_RETURN_NONE;
}


static int
FSPoll_tp_init(FSPoll *self, PyObject *args, PyObject *kwargs)
{
    int r;
    Loop *loop;

    UNUSED_ARG(kwargs);

    RAISE_IF_HANDLE_INITIALIZED(self, -1);

    if (!PyArg_ParseTuple(args, "O!:__init__", &LoopType, &loop)) {
        return -1;
    }

    r = uv_fs_poll_init(loop->uv_loop, &self->fspoll_h);
    if (r != 0) {
        RAISE_UV_EXCEPTION(loop->uv_loop, PyExc_FSPollError);
        return -1;
    }

    initialize_handle(HANDLE(self), loop);

    return 0;
}


static PyObject *
FSPoll_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    FSPoll *self;

    self = (FSPoll *)HandleType.tp_new(type, args, kwargs);
    if (!self) {
        return NULL;
    }

    self->fspoll_h.data = self;
    UV_HANDLE(self) = (uv_handle_t *)&self->fspoll_h;

    return (PyObject *)self;
}


static int
FSPoll_tp_traverse(FSPoll *self, visitproc visit, void *arg)
{
    Py_VISIT(self->callback);
    return HandleType.tp_traverse((PyObject *)self, visit, arg);
}


static int
FSPoll_tp_clear(FSPoll *self)
{
    Py_CLEAR(self->callback);
    return HandleType.tp_clear((PyObject *)self);
}


static PyMethodDef
FSPoll_tp_methods[] = {
    { "start", (PyCFunction)FSPoll_func_start, METH_VARARGS | METH_KEYWORDS, "Start the FSPoll handle." },
    { "stop", (PyCFunction)FSPoll_func_stop, METH_NOARGS, "Stop the FSPoll handle." },
    { NULL }
};


static PyTypeObject FSPollType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyuv.fs.FSPoll",                                               /*tp_name*/
    sizeof(FSPoll),                                                 /*tp_basicsize*/
    0,                                                              /*tp_itemsize*/
    0,                                                              /*tp_dealloc*/
    0,                                                              /*tp_print*/
    0,                                                              /*tp_getattr*/
    0,                                                              /*tp_setattr*/
    0,                                                              /*tp_compare*/
    0,                                                              /*tp_repr*/
    0,                                                              /*tp_as_number*/
    0,                                                              /*tp_as_sequence*/
    0,                                                              /*tp_as_mapping*/
    0,                                                              /*tp_hash */
    0,                                                              /*tp_call*/
    0,                                                              /*tp_str*/
    0,                                                              /*tp_getattro*/
    0,                                                              /*tp_setattro*/
    0,                                                              /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,  /*tp_flags*/
    0,                                                              /*tp_doc*/
    (traverseproc)FSPoll_tp_traverse,                               /*tp_traverse*/
    (inquiry)FSPoll_tp_clear,                                       /*tp_clear*/
    0,                                                              /*tp_richcompare*/
    0,                                                              /*tp_weaklistoffset*/
    0,                                                              /*tp_iter*/
    0,                                                              /*tp_iternext*/
    FSPoll_tp_methods,                                              /*tp_methods*/
    0,                                                              /*tp_members*/
    0,                                                              /*tp_getsets*/
    0,                                                              /*tp_base*/
    0,                                                              /*tp_dict*/
    0,                                                              /*tp_descr_get*/
    0,                                                              /*tp_descr_set*/
    0,                                                              /*tp_dictoffset*/
    (initproc)FSPoll_tp_init,                                       /*tp_init*/
    0,                                                              /*tp_alloc*/
    FSPoll_tp_new,                                                  /*tp_new*/
};



#ifdef PYUV_PYTHON3
static PyModuleDef pyuv_fs_module = {
    PyModuleDef_HEAD_INIT,
    "pyuv.fs",              /*m_name*/
    NULL,                   /*m_doc*/
    -1,                     /*m_size*/
    FS_methods,             /*m_methods*/
};
#endif

PyObject *
init_fs(void)
{
    PyObject *module;
#ifdef PYUV_PYTHON3
    module = PyModule_Create(&pyuv_fs_module);
#else
    module = Py_InitModule("pyuv.fs", FS_methods);
#endif
    if (module == NULL) {
        return NULL;
    }

    PyModule_AddIntMacro(module, UV_RENAME);
    PyModule_AddIntMacro(module, UV_CHANGE);
    PyModule_AddIntMacro(module, UV_FS_EVENT_WATCH_ENTRY);
    PyModule_AddIntMacro(module, UV_FS_EVENT_STAT);
    PyModule_AddIntMacro(module, UV_FS_SYMLINK_DIR);
    PyModule_AddIntMacro(module, UV_FS_SYMLINK_JUNCTION);

    FSEventType.tp_base = &HandleType;
    FSPollType.tp_base = &HandleType;

    PyUVModule_AddType(module, "FSEvent", &FSEventType);
    PyUVModule_AddType(module, "FSPoll", &FSPollType);

    /* initialize PyStructSequence types */
    if (StatResultType.tp_name == 0)
        PyStructSequence_InitType(&StatResultType, &stat_result_desc);

    return module;
}

