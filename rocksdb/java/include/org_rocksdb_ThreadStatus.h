/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_rocksdb_ThreadStatus */

#ifndef _Included_org_rocksdb_ThreadStatus
#define _Included_org_rocksdb_ThreadStatus
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    getThreadTypeName
 * Signature: (B)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_getThreadTypeName
  (JNIEnv *, jclass, jbyte);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    getOperationName
 * Signature: (B)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_getOperationName
  (JNIEnv *, jclass, jbyte);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    microsToStringNative
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_microsToStringNative
  (JNIEnv *, jclass, jlong);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    getOperationStageName
 * Signature: (B)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_getOperationStageName
  (JNIEnv *, jclass, jbyte);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    getOperationPropertyName
 * Signature: (BI)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_getOperationPropertyName
  (JNIEnv *, jclass, jbyte, jint);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    interpretOperationProperties
 * Signature: (B[J)Ljava/util/Map;
 */
JNIEXPORT jobject JNICALL Java_org_rocksdb_ThreadStatus_interpretOperationProperties
  (JNIEnv *, jclass, jbyte, jlongArray);

/*
 * Class:     org_rocksdb_ThreadStatus
 * Method:    getStateName
 * Signature: (B)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_ThreadStatus_getStateName
  (JNIEnv *, jclass, jbyte);

#ifdef __cplusplus
}
#endif
#endif
