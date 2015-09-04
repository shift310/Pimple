static void pimple_closure_free_object_storage(zend_object *zobj)
{
	pimple_closure_object *obj = FETCH_CUSTOM_OBJ(zobj, pimple_closure_object);

	if (!Z_ISUNDEF(obj->factory)) {
		zval_ptr_dtor(&obj->factory);
	}
	if (!Z_ISUNDEF(obj->callable)) {
		zval_ptr_dtor(&obj->callable);
	}
	zend_object_std_dtor(zobj);
}

static zend_object *pimple_closure_object_create(zend_class_entry *ce)
{
	pimple_closure_object *pimple_closure_obj = NULL;

	pimple_closure_obj = ecalloc(1, sizeof(pimple_closure_object) + zend_object_properties_size(ce));
	zend_object_std_init(&pimple_closure_obj->zobj, ce);
	object_properties_init(&pimple_closure_obj->zobj, ce);

	pimple_closure_obj->zobj.handlers = &pimple_closure_object_handlers;

	return &pimple_closure_obj->zobj;
}

static zend_function *pimple_closure_get_constructor(zend_object *obj)
{
	zend_error(E_ERROR, "Pimple\\ContainerClosure is an internal class and cannot be instantiated");

	return NULL;
}

static int pimple_closure_get_closure(zval *obj, zend_class_entry **ce_ptr, union _zend_function **fptr_ptr, zend_object **zobj)
{
	*zobj     = Z_OBJ_P(obj);
	*ce_ptr   = Z_OBJCE_P(obj);
	*fptr_ptr = (zend_function *)&pimple_closure_invoker_function;
	(*fptr_ptr)->common.prototype = (zend_function *)Z_OBJ_P(obj);

	return SUCCESS;
}

/*
 * This is PHP code snippet handling extend()s calls :

  $extended = function ($c) use ($callable, $factory) {
      return $callable($factory($c), $c);
  };

 */
PHP_METHOD(PimpleClosure, invoker)
{
	zval *arg = NULL, retval = {0}, newretval = {0};
	zend_fcall_info fci      = {0};
	zval args[2];
	FETCH_CUSTOM_OBJ_THIS(pimple_closure_object)

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &arg) == FAILURE) {
		OBJ_RELEASE((zend_object*)execute_data->func->common.prototype);
		return;
	}

	ZVAL_COPY_VALUE(&args[0], arg);
	fci.function_name = obj->factory;
	fci.params        = args;
	fci.param_count   = 1;
	fci.retval        = &retval;
	fci.size = sizeof(fci);

	if (zend_call_function(&fci, NULL) == FAILURE || EG(exception)) {
		OBJ_RELEASE((zend_object*)execute_data->func->common.prototype);
		RETVAL_ZVAL(&EG(uninitialized_zval), 1, 0);
		return;
	}

	memset(&fci, 0, sizeof(fci));
	fci.size = sizeof(fci);

	ZVAL_COPY_VALUE(&args[0], &retval);
	ZVAL_COPY_VALUE(&args[1], arg);

	fci.function_name = obj->callable;
	fci.params        = args;
	fci.param_count   = 2;
	fci.retval = &newretval;

	if (zend_call_function(&fci, NULL) == FAILURE || EG(exception)) {
		zval_ptr_dtor(&retval);
		OBJ_RELEASE((zend_object*)execute_data->func->common.prototype);
		RETVAL_ZVAL(&EG(uninitialized_zval), 1, 0);
		return;
	}

	zval_ptr_dtor(&retval);

	RETVAL_ZVAL(&newretval, 0 ,0);
	OBJ_RELEASE((zend_object*)execute_data->func->common.prototype);
}
