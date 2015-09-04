
/*
 * This file is part of Pimple.
 *
 * Copyright (c) 2014 Fabien Potencier
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_pimple.h"
#include "zend_interfaces.h"
#include "zend.h"
#include "Zend/zend_closures.h"
#include "ext/spl/spl_exceptions.h"
#include "Zend/zend_exceptions.h"
#include "main/php_output.h"
#include "SAPI.h"

static zend_class_entry *pimple_ce;
static zend_object_handlers pimple_object_handlers;
static zend_class_entry *pimple_closure_ce;
static zend_class_entry *pimple_serviceprovider_ce;
static zend_object_handlers pimple_closure_object_handlers;
static zend_internal_function pimple_closure_invoker_function;

#define PIMPLE_OBJECT_HANDLE_INHERITANCE_OBJECT_HANDLERS	do { \
	if (ce != pimple_ce) { \
		zval *function; \
		function = zend_hash_str_find(&ce->function_table, ZEND_STRL("offsetget")); \
		if (((zend_function *)Z_PTR_P(function))->common.scope != ce) { /* if the function is not defined in this actual class */ \
			pimple_object_handlers.read_dimension = pimple_object_read_dimension; /* then overwrite the handler to use custom one */ \
		} \
		function = zend_hash_str_find(&ce->function_table, ZEND_STRL("offsetset")); \
		if (((zend_function *)Z_PTR_P(function))->common.scope != ce) { \
			pimple_object_handlers.write_dimension = pimple_object_write_dimension; \
		} \
		function = zend_hash_str_find(&ce->function_table, ZEND_STRL("offsetexists")); \
		if (((zend_function *)Z_PTR_P(function))->common.scope != ce) { \
			pimple_object_handlers.has_dimension = pimple_object_has_dimension; \
		} \
		function = zend_hash_str_find(&ce->function_table, ZEND_STRL("offsetunset")); \
		if (((zend_function *)Z_PTR_P(function))->common.scope != ce) { \
			pimple_object_handlers.unset_dimension = pimple_object_unset_dimension; \
		} \
	} else { \
		pimple_object_handlers.read_dimension  = pimple_object_read_dimension; \
		pimple_object_handlers.write_dimension = pimple_object_write_dimension; \
		pimple_object_handlers.has_dimension   = pimple_object_has_dimension; \
		pimple_object_handlers.unset_dimension = pimple_object_unset_dimension; \
	}\
											} while(0);

#define PIMPLE_CALL_CB	do { \
			zend_fcall_info_argn(&fci, 1, object); \
			fci.size           = sizeof(fci); \
			fci.object         = bucket->fcc.object; \
			fci.function_name  = bucket->value; \
			fci.no_separation  = 1; \
			fci.retval         = &retval; \
\
			zend_call_function(&fci, &bucket->fcc); \
			Z_TRY_DELREF_P(object); \
			efree(fci.params); \
			if (EG(exception)) { \
				return &EG(uninitialized_zval); \
			} \
						} while(0);

ZEND_BEGIN_ARG_INFO_EX(arginfo___construct, 0, 0, 0)
ZEND_ARG_ARRAY_INFO(0, value, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_offsetset, 0, 0, 2)
ZEND_ARG_INFO(0, offset)
ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_offsetget, 0, 0, 1)
ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_offsetexists, 0, 0, 1)
ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_offsetunset, 0, 0, 1)
ZEND_ARG_INFO(0, offset)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_factory, 0, 0, 1)
ZEND_ARG_INFO(0, callable)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_protect, 0, 0, 1)
ZEND_ARG_INFO(0, callable)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_raw, 0, 0, 1)
ZEND_ARG_INFO(0, id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_extend, 0, 0, 2)
ZEND_ARG_INFO(0, id)
ZEND_ARG_INFO(0, callable)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_keys, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_register, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, provider, Pimple\\ServiceProviderInterface, 0)
ZEND_ARG_ARRAY_INFO(0, values, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_serviceprovider_register, 0, 0, 1)
ZEND_ARG_OBJ_INFO(0, pimple, Pimple\\Container, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_pimple_closure, u)
ZEND_ARG_INFO(0, arg)
ZEND_END_ARG_INFO()

static const zend_function_entry pimple_ce_functions[] = {
	PHP_ME(Pimple, __construct,	arginfo___construct, ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, factory,         arginfo_factory,         ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, protect,         arginfo_protect,         ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, raw,             arginfo_raw,             ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, extend,          arginfo_extend,          ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, keys,            arginfo_keys,            ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, register,        arginfo_register,        ZEND_ACC_PUBLIC)

	PHP_ME(Pimple, offsetSet,       arginfo_offsetset,       ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, offsetGet,       arginfo_offsetget,       ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, offsetExists,    arginfo_offsetexists,    ZEND_ACC_PUBLIC)
	PHP_ME(Pimple, offsetUnset,     arginfo_offsetunset,     ZEND_ACC_PUBLIC)
	PHP_FE_END
};

static const zend_function_entry pimple_serviceprovider_iface_ce_functions[] = {
	PHP_ABSTRACT_ME(ServiceProviderInterface, register, arginfo_serviceprovider_register)
	PHP_FE_END
};

#include "pimple_closure.c"

static void pimple_free_object_storage(zend_object *zobj)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ(zobj, pimple_object);

	zend_hash_destroy(&obj->factories);
	zend_hash_destroy(&obj->protected_values);
	zend_hash_destroy(&obj->values);
	zend_object_std_dtor(zobj);

	if (obj->gc_data) {
		efree(obj->gc_data);
	}
}

static void pimple_free_bucket(pimple_bucket_value *bucket)
{
	if (!Z_ISUNDEF(bucket->raw)) {
		zval_ptr_dtor(&bucket->raw);
	}
	if (!Z_ISUNDEF(bucket->value)) {
		zval_ptr_dtor(&bucket->value);
	}
}

static uint32_t pimple_gc_data_num(pimple_object *obj)
{
	return 2 + 2 * zend_hash_num_elements(&obj->values);
}

static HashTable *pimple_get_gc(zval *object, zval **table, int *n)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ_ZVAL(object, pimple_object);

	uint32_t i = 0, new_gc_data_num = pimple_gc_data_num(obj);
	pimple_bucket_value *bucket;

	if (new_gc_data_num > obj->gc_data_num) {
		obj->gc_data = safe_erealloc(obj->gc_data, sizeof(zval), new_gc_data_num, 0);
		obj->gc_data_num = new_gc_data_num;
	}

	ZVAL_ARR(&obj->gc_data[i++], &obj->factories);
	ZVAL_ARR(&obj->gc_data[i++], &obj->protected_values);

	ZEND_HASH_FOREACH_PTR(&obj->values, bucket) {
		ZVAL_COPY_VALUE(&obj->gc_data[i++], &bucket->value);
		ZVAL_COPY_VALUE(&obj->gc_data[i++], &bucket->raw);
	} ZEND_HASH_FOREACH_END();

	*table = obj->gc_data;
	*n     = obj->gc_data_num;

	return zend_std_get_properties(object);
}

static zend_object *pimple_object_create(zend_class_entry *ce)
{
	pimple_object *pimple_obj  = NULL;
	zval zv, object;

	pimple_obj = ecalloc(1, sizeof(pimple_object) + zend_object_properties_size(ce));
	zend_object_std_init(&pimple_obj->zobj, ce);
	object_properties_init(&pimple_obj->zobj, ce);
	PIMPLE_OBJECT_HANDLE_INHERITANCE_OBJECT_HANDLERS
	pimple_obj->zobj.handlers = &pimple_object_handlers;

	zend_hash_init(&pimple_obj->factories, PIMPLE_DEFAULT_ZVAL_CACHE_NUM, NULL, pimple_bucket_dtor, 0);
	zend_hash_init(&pimple_obj->protected_values, PIMPLE_DEFAULT_ZVAL_CACHE_NUM, NULL, pimple_bucket_dtor, 0);
	zend_hash_init(&pimple_obj->values, PIMPLE_DEFAULT_ZVAL_VALUES_NUM, NULL, pimple_bucket_dtor, 0);

	return &pimple_obj->zobj;
}

static void pimple_object_write_dimension(zval *object, zval *offset, zval *value)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ_ZVAL(object, pimple_object);

	pimple_bucket_value pimple_value = {0}, *found_value = NULL;

	pimple_zval_to_pimpleval(value, &pimple_value);

	if (!offset) {/* $p[] = 'foo' when not overloaded */
		zend_hash_next_index_insert_mem(&obj->values, (void *)&pimple_value, sizeof(pimple_bucket_value));
		return;
	}

	switch (Z_TYPE_P(offset)) {
	case IS_STRING:
		found_value = (pimple_bucket_value *)zend_hash_find_ptr(&obj->values, Z_STR_P(offset));
		if (found_value && found_value->type == PIMPLE_IS_SERVICE && found_value->initialized == 1) {
			pimple_free_bucket(&pimple_value);
			zend_throw_exception_ex(spl_ce_RuntimeException, 0, "Cannot override frozen service \"%s\".", Z_STRVAL_P(offset));
			return;
		}
		if (!zend_hash_update_mem(&obj->values, Z_STR_P(offset), (void *)&pimple_value, sizeof(pimple_bucket_value))) {
			pimple_free_bucket(&pimple_value);
			return;
		}
	break;
	case IS_DOUBLE:
	case IS_TRUE: case IS_FALSE:
	case IS_LONG: {
		ulong index;
		if (Z_TYPE_P(offset) == IS_DOUBLE) {
			index = (ulong)Z_DVAL_P(offset);
		} else {
			index = Z_LVAL_P(offset);
		}
		found_value = (pimple_bucket_value *)zend_hash_index_find_ptr(&obj->values, index);
		if (found_value && found_value->type == PIMPLE_IS_SERVICE && found_value->initialized == 1) {
			pimple_free_bucket(&pimple_value);
			zend_throw_exception_ex(spl_ce_RuntimeException, 0, "Cannot override frozen service \"%ld\".", index);
			return;
		}
		if (!zend_hash_index_update_mem(&obj->values, index, (void *)&pimple_value, sizeof(pimple_bucket_value))) {
			pimple_free_bucket(&pimple_value);
			return;
		}
	}
	break;
	case IS_NULL: /* $p[] = 'foo' when overloaded */
		zend_hash_next_index_insert_mem(&obj->values, (void *)&pimple_value, sizeof(pimple_bucket_value));
	break;
	default:
		pimple_free_bucket(&pimple_value);
		zend_error(E_WARNING, "Unsupported offset type");
	}
}

static void pimple_object_unset_dimension(zval *object, zval *offset)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ_ZVAL(object, pimple_object);

	switch (Z_TYPE_P(offset)) {
	case IS_STRING:
		zend_symtable_del(&obj->values, Z_STR_P(offset));
		zend_symtable_del(&obj->factories, Z_STR_P(offset));
		zend_symtable_del(&obj->protected_values, Z_STR_P(offset));
	break;
	case IS_DOUBLE:
	case IS_TRUE: case IS_FALSE:
	case IS_LONG: {
		ulong index;
		if (Z_TYPE_P(offset) == IS_DOUBLE) {
			index = (ulong)Z_DVAL_P(offset);
		} else {
			index = Z_LVAL_P(offset);
		}
		zend_hash_index_del(&obj->values, index);
		zend_hash_index_del(&obj->factories, index);
		zend_hash_index_del(&obj->protected_values, index);
	}
	break;
	default:
		zend_error(E_WARNING, "Unsupported offset type");
	}
}

static int pimple_object_has_dimension(zval *object, zval *offset, int check_empty)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ_ZVAL(object, pimple_object);

	pimple_bucket_value *retval = NULL;

	switch (Z_TYPE_P(offset)) {
	case IS_STRING:
		if ((retval = (pimple_bucket_value *)zend_symtable_find(&obj->values, Z_STR_P(offset)))) {
			switch (check_empty) {
			case 0: /* isset */
				return 1; /* Differs from PHP behavior (Z_TYPE_P(retval->value) != IS_NULL;) */
			case 1: /* empty */
			default:
				return zend_is_true(&((pimple_bucket_value *)Z_PTR_P((zval *)retval))->value);
			}
		}
		return 0;
	break;
	case IS_DOUBLE:
	case IS_TRUE: case IS_FALSE:
	case IS_LONG: {
		ulong index;
		if (Z_TYPE_P(offset) == IS_DOUBLE) {
			index = (ulong)Z_DVAL_P(offset);
		} else {
			index = Z_LVAL_P(offset);
		}
		if ((retval = (pimple_bucket_value *)zend_hash_index_find_ptr(&obj->values, index))) {
			switch (check_empty) {
				case 0: /* isset */
					return 1; /* Differs from PHP behavior (Z_TYPE_P(retval->value) != IS_NULL;)*/
				case 1: /* empty */
				default:
					return zend_is_true(&retval->value);
			}
		}
		return 0;
	}
	break;
	default:
		zend_error(E_WARNING, "Unsupported offset type");
		return 0;
	}
}

static zval *pimple_object_read_dimension(zval *object, zval *offset, int type, zval *rv)
{
	pimple_object *obj = FETCH_CUSTOM_OBJ_ZVAL(object, pimple_object);

	pimple_bucket_value *bucket = NULL;
	zend_fcall_info fci         = {0};
	zval retval                 = {0};

	switch (Z_TYPE_P(offset)) {
	case IS_STRING:
		if (!(bucket = (pimple_bucket_value *)zend_symtable_find(&obj->values, Z_STR_P(offset)))) {
			zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%s\" is not defined.", Z_STRVAL_P(offset));
			return &EG(uninitialized_zval);
		} else {
			bucket = (pimple_bucket_value *)Z_PTR_P((zval *)bucket);
		}
	break;
	case IS_DOUBLE:
	case IS_TRUE: case IS_FALSE:
	case IS_LONG: {
		ulong index;
		if (Z_TYPE_P(offset) == IS_DOUBLE) {
			index = (ulong)Z_DVAL_P(offset);
		} else {
			index = Z_LVAL_P(offset);
		}
		if (!(bucket = (pimple_bucket_value *)zend_hash_index_find_ptr(&obj->values, index))) {
			return &EG(uninitialized_zval);
		}
	}
	break;
	case IS_NULL: /* $p[][3] = 'foo' first dim access */
		return &EG(uninitialized_zval);
	break;
	default:
		zend_error(E_WARNING, "Unsupported offset type");
		return &EG(uninitialized_zval);
	}

	if (bucket->type == PIMPLE_IS_PARAM) {
		return &bucket->value;
	}

	if (zend_hash_index_exists(&obj->protected_values, bucket->handle_num)) {
		/* Service is protected, return the value every time */
		return &bucket->value;
	}

	if (zend_hash_index_exists(&obj->factories, bucket->handle_num)) {
		/* Service is a factory, call it everytime and never cache its result */
		PIMPLE_CALL_CB
		ZVAL_COPY_VALUE(rv, &retval);
		return rv;
	}

	if (bucket->initialized) {
		/* Service has already been called, return its cached value */
		return &bucket->value;
	}

	ZVAL_COPY(&bucket->raw, &bucket->value);

	PIMPLE_CALL_CB

	bucket->initialized = 1;
	zval_ptr_dtor(&bucket->value);
	ZVAL_COPY_VALUE(&bucket->value, &retval);

	return &bucket->value;
}

static int pimple_zval_is_valid_callback(zval *_zval, pimple_bucket_value *_pimple_bucket_value)
{
	if (Z_TYPE_P(_zval) != IS_OBJECT) {
		return FAILURE;
	}

	if (_pimple_bucket_value->fcc.called_scope) {
		return SUCCESS;
	}

	if (Z_OBJ_HANDLER_P(_zval, get_closure) && Z_OBJ_HANDLER_P(_zval, get_closure)(_zval, &_pimple_bucket_value->fcc.calling_scope, &_pimple_bucket_value->fcc.function_handler, &_pimple_bucket_value->fcc.object) == SUCCESS) {
		_pimple_bucket_value->fcc.called_scope = _pimple_bucket_value->fcc.calling_scope;
		return SUCCESS;
	} else {
		return FAILURE;
	}
}

static int pimple_zval_to_pimpleval(zval *_zval, pimple_bucket_value *_pimple_bucket_value)
{
	ZVAL_COPY(&_pimple_bucket_value->value, _zval);

	if (Z_TYPE_P(_zval) != IS_OBJECT) {
		return PIMPLE_IS_PARAM;
	}

	if (pimple_zval_is_valid_callback(_zval, _pimple_bucket_value) == SUCCESS) {
		_pimple_bucket_value->type       = PIMPLE_IS_SERVICE;
		_pimple_bucket_value->handle_num = Z_OBJ_HANDLE_P(_zval);
	}

	return PIMPLE_IS_SERVICE;
}

static void pimple_bucket_dtor(zval *bucket)
{
	pimple_bucket_value *b = Z_PTR_P(bucket);

	pimple_free_bucket(b);
	efree(b);
}

PHP_METHOD(Pimple, protect)
{
	zval *protected     = NULL;
	pimple_bucket_value bucket = {0};
	FETCH_CUSTOM_OBJ_THIS(pimple_object)

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &protected) == FAILURE) {
		return;
	}

	if (pimple_zval_is_valid_callback(protected, &bucket) == FAILURE) {
		pimple_free_bucket(&bucket);
		zend_throw_exception(spl_ce_InvalidArgumentException, "Callable is not a Closure or invokable object.", 0);
		return;
	}

	pimple_zval_to_pimpleval(protected, &bucket);

	if (zend_hash_index_update_mem(&obj->protected_values, (ulong)bucket.handle_num, (void *)&bucket, sizeof(pimple_bucket_value))) {
		RETURN_ZVAL(protected, 1 , 0);
	} else {
		pimple_free_bucket(&bucket);
	}
	RETURN_FALSE;
}

PHP_METHOD(Pimple, raw)
{
	zval *offset = NULL;
	pimple_bucket_value *value = NULL;
	FETCH_CUSTOM_OBJ_THIS(pimple_object)

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &offset) == FAILURE) {
		return;
	}

	switch (Z_TYPE_P(offset)) {
		case IS_STRING:
			if (!(value = (pimple_bucket_value *)zend_symtable_find(&obj->values, Z_STR_P(offset)))) {
				zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%s\" is not defined.", Z_STRVAL_P(offset));
				RETURN_NULL();
			} else {
				value = (pimple_bucket_value *)Z_PTR_P((zval *)value);
			}
		break;
		case IS_DOUBLE:
		case IS_TRUE: case IS_FALSE:
		case IS_LONG: {
			ulong index;
			if (Z_TYPE_P(offset) == IS_DOUBLE) {
				index = (ulong)Z_DVAL_P(offset);
			} else {
				index = Z_LVAL_P(offset);
			}
			if (!(value = zend_hash_index_find_ptr(&obj->values, index))) {
				RETURN_NULL();
			}
		}
		break;
		case IS_NULL:
		default:
			zend_error(E_WARNING, "Unsupported offset type");
	}

	if (!Z_ISUNDEF(value->raw)) {
		RETVAL_ZVAL(&value->raw, 1, 0);
	} else {
		RETVAL_ZVAL(&value->value, 1, 0);
	}
}

PHP_METHOD(Pimple, extend)
{
	zval *offset = NULL, *callable = NULL, pimple_closure_obj;
	pimple_bucket_value bucket = {0}, *value = NULL;
	pimple_closure_object *pcobj = NULL;
	FETCH_CUSTOM_OBJ_THIS(pimple_object)

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &offset, &callable) == FAILURE) {
		return;
	}

	switch (Z_TYPE_P(offset)) {
		case IS_STRING:
			if (!(value = (pimple_bucket_value *)zend_symtable_find(&obj->values, Z_STR_P(offset)))) {
				zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%s\" is not defined.", Z_STRVAL_P(offset));
				RETURN_NULL();
			}
			if (value->type != PIMPLE_IS_SERVICE) {
				zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%s\" does not contain an object definition.", Z_STRVAL_P(offset));
				RETURN_NULL();
			}
		break;
		case IS_DOUBLE:
		case IS_TRUE: case IS_FALSE:
		case IS_LONG: {
			ulong index;
			if (Z_TYPE_P(offset) == IS_DOUBLE) {
				index = (ulong)Z_DVAL_P(offset);
			} else {
				index = Z_LVAL_P(offset);
			}
			if (!(value = (pimple_bucket_value *)zend_hash_index_find_ptr(&obj->values, index))) {
				zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%ld\" is not defined.", index);
				RETURN_NULL();
			}
			if (value->type != PIMPLE_IS_SERVICE) {
				zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0, "Identifier \"%ld\" does not contain an object definition.", index);
				RETURN_NULL();
			}
		}
		break;
		case IS_NULL:
		default:
			zend_error(E_WARNING, "Unsupported offset type");
	}

	if (pimple_zval_is_valid_callback(callable, &bucket) == FAILURE) {
		pimple_free_bucket(&bucket);
		zend_throw_exception(spl_ce_InvalidArgumentException, "Extension service definition is not a Closure or invokable object.", 0);
		RETURN_NULL();
	}
	pimple_free_bucket(&bucket);

	object_init_ex(&pimple_closure_obj, pimple_closure_ce);

	pcobj = FETCH_CUSTOM_OBJ_ZVAL(&pimple_closure_obj, pimple_closure_object);
	ZVAL_COPY(&pcobj->callable, callable);
	ZVAL_COPY(&pcobj->factory, &value->value);

	if (zend_hash_index_exists(&obj->factories, value->handle_num)) {
		pimple_zval_to_pimpleval(&pimple_closure_obj, &bucket);
		zend_hash_index_del(&obj->factories, value->handle_num);
		zend_hash_index_update_mem(&obj->factories, bucket.handle_num, (void *)&bucket, sizeof(pimple_bucket_value));
		Z_ADDREF(pimple_closure_obj);
	}

	pimple_object_write_dimension(getThis(), offset, &pimple_closure_obj);

	RETVAL_ZVAL(&pimple_closure_obj, 0, 0);
}

PHP_METHOD(Pimple, keys)
{
	zval *value, endval;
	zend_string *str_index;
	ulong num_index;
	FETCH_CUSTOM_OBJ_THIS(pimple_object)

	if (zend_parse_parameters_none() == FAILURE) {
		return;
	}

	array_init_size(return_value, zend_hash_num_elements(&obj->values));

	ZEND_HASH_FOREACH_KEY_VAL(&obj->values, num_index, str_index, value)
		if (str_index) {
			ZVAL_STR(&endval, str_index);
		} else {
			ZVAL_LONG(&endval, num_index);
		}
		zend_hash_next_index_insert(Z_ARRVAL_P(return_value), &endval);
	ZEND_HASH_FOREACH_END();
}

PHP_METHOD(Pimple, factory)
{
	zval *factory       = NULL;
	pimple_bucket_value bucket = {0};
	FETCH_CUSTOM_OBJ_THIS(pimple_object)

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &factory) == FAILURE) {
		return;
	}

	if (pimple_zval_is_valid_callback(factory, &bucket) == FAILURE) {
		pimple_free_bucket(&bucket);
		zend_throw_exception(spl_ce_InvalidArgumentException, "Service definition is not a Closure or invokable object.", 0);
		return;
	}

	pimple_zval_to_pimpleval(factory, &bucket);

	if (zend_hash_index_update_mem(&obj->factories, bucket.handle_num, (void *)&bucket, sizeof(bucket))) {
		RETURN_ZVAL(factory, 1 , 0);
	} else {
		pimple_free_bucket(&bucket);
	}

	RETURN_FALSE;
}

PHP_METHOD(Pimple, offsetSet)
{
	zval *offset = NULL, *value = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "zz", &offset, &value) == FAILURE) {
		return;
	}

	pimple_object_write_dimension(getThis(), offset, value);
}

PHP_METHOD(Pimple, offsetGet)
{
	zval *offset = NULL, *retval = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &offset) == FAILURE) {
		return;
	}

	retval = pimple_object_read_dimension(getThis(), offset, 0, NULL);

	RETVAL_ZVAL(retval, 1, 0);
}

PHP_METHOD(Pimple, offsetUnset)
{
	zval *offset = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &offset) == FAILURE) {
		return;
	}

	pimple_object_unset_dimension(getThis(), offset);
}

PHP_METHOD(Pimple, offsetExists)
{
	zval *offset = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &offset) == FAILURE) {
		return;
	}

	RETVAL_BOOL(pimple_object_has_dimension(getThis(), offset, 1));
}

PHP_METHOD(Pimple, register)
{
	zval *provider;
	zval *data;
	zval retval = {0};
	zval key    = {0};

	HashTable *array = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "O|h", &provider, pimple_serviceprovider_ce, &array) == FAILURE) {
		return;
	}

	RETVAL_ZVAL(getThis(), 1, 0);

	zend_call_method_with_1_params(provider, Z_OBJCE_P(provider), NULL, "register", &retval, getThis());

	if (!Z_ISUNDEF_P(&retval)) {
		zval_ptr_dtor(&retval);
	}

	if (!array) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(array, data)
		zend_hash_get_current_key_zval_ex(array, &key, &array->nInternalPointer);
		pimple_object_write_dimension(getThis(), &key, data);
	ZEND_HASH_FOREACH_END();
}

PHP_METHOD(Pimple, __construct)
{
	HashTable *values = NULL;
	zend_ulong h;
	zend_string *key;
	zval *data, new_key;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|h", &values) == FAILURE || !values) {
		return;
	}

	ZEND_HASH_FOREACH_KEY_VAL(values, h, key, data)
		if (!key) {
			ZVAL_LONG(&new_key, h);
		} else {
			ZVAL_STR(&new_key, key);
		}
		pimple_object_write_dimension(getThis(), &new_key, data);
	ZEND_HASH_FOREACH_END();
}

PHP_MINIT_FUNCTION(pimple)
{
	zend_class_entry tmp_pimple_ce, tmp_pimple_closure_ce, tmp_pimple_serviceprovider_iface_ce;
	INIT_NS_CLASS_ENTRY(tmp_pimple_ce, PIMPLE_NS, "Container", pimple_ce_functions);
	INIT_NS_CLASS_ENTRY(tmp_pimple_closure_ce, PIMPLE_NS, "ContainerClosure", NULL);
	INIT_NS_CLASS_ENTRY(tmp_pimple_serviceprovider_iface_ce, PIMPLE_NS, "ServiceProviderInterface", pimple_serviceprovider_iface_ce_functions);

	tmp_pimple_ce.create_object         = pimple_object_create;
	tmp_pimple_closure_ce.create_object = pimple_closure_object_create;

	pimple_ce = zend_register_internal_class(&tmp_pimple_ce);
	zend_class_implements(pimple_ce, 1, zend_ce_arrayaccess);

	pimple_closure_ce            = zend_register_internal_class(&tmp_pimple_closure_ce);
	pimple_closure_ce->ce_flags |= ZEND_ACC_FINAL;

	pimple_serviceprovider_ce = zend_register_internal_interface(&tmp_pimple_serviceprovider_iface_ce);

	memcpy(&pimple_closure_object_handlers, zend_get_std_object_handlers(), sizeof(*zend_get_std_object_handlers()));
	memcpy(&pimple_object_handlers, zend_get_std_object_handlers(), sizeof(*zend_get_std_object_handlers()));

	pimple_closure_object_handlers.get_closure     = pimple_closure_get_closure;
	pimple_closure_object_handlers.offset          = XtOffsetOf(pimple_closure_object, zobj);
	pimple_closure_object_handlers.get_constructor = pimple_closure_get_constructor;
	pimple_closure_object_handlers.free_obj        = pimple_closure_free_object_storage;

	pimple_object_handlers.offset          = XtOffsetOf(pimple_object, zobj);
	pimple_object_handlers.free_obj        = pimple_free_object_storage;
	pimple_object_handlers.get_gc          = pimple_get_gc;

	pimple_closure_invoker_function.function_name     = zend_string_init("Pimple closure internal invoker", strlen("Pimple closure internal invoker"), 1);
	pimple_closure_invoker_function.fn_flags         |= ZEND_ACC_CLOSURE;
	pimple_closure_invoker_function.handler           = ZEND_MN(PimpleClosure_invoker);
	pimple_closure_invoker_function.num_args          = 1;
	pimple_closure_invoker_function.arg_info          = (zend_internal_arg_info *)arginfo_pimple_closure;
	pimple_closure_invoker_function.required_num_args = 1;
	pimple_closure_invoker_function.scope             = pimple_closure_ce;
	pimple_closure_invoker_function.type              = ZEND_INTERNAL_FUNCTION;
	pimple_closure_invoker_function.module            = &pimple_module_entry;

	return SUCCESS;
}

PHP_MINFO_FUNCTION(pimple)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "SensioLabs Pimple C support", "enabled");
	php_info_print_table_row(2, "Pimple supported version", PIMPLE_VERSION);
	php_info_print_table_end();

	php_info_print_box_start(0);
	php_write((void *)ZEND_STRL("SensioLabs Pimple C support developed by Julien Pauli"));
	if (!sapi_module.phpinfo_as_text) {
		php_write((void *)ZEND_STRL(sensiolabs_logo));
	}
	php_info_print_box_end();
}

zend_module_entry pimple_module_entry = {
	STANDARD_MODULE_HEADER,
	"pimple",
	NULL,
	PHP_MINIT(pimple),
	NULL,
	NULL,
	NULL,
	PHP_MINFO(pimple),
	PIMPLE_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PIMPLE
ZEND_GET_MODULE(pimple)
#endif
