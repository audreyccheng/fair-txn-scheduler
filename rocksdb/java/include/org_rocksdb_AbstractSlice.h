/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_rocksdb_AbstractSlice */

#ifndef _Included_org_rocksdb_AbstractSlice
#define _Included_org_rocksdb_AbstractSlice
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    createNewSliceFromString
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_AbstractSlice_createNewSliceFromString
  (JNIEnv *, jclass, jstring);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    size0
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_org_rocksdb_AbstractSlice_size0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    empty0
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_AbstractSlice_empty0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    toString0
 * Signature: (JZ)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_AbstractSlice_toString0
  (JNIEnv *, jobject, jlong, jboolean);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    compare0
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_org_rocksdb_AbstractSlice_compare0
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    startsWith0
 * Signature: (JJ)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_AbstractSlice_startsWith0
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     org_rocksdb_AbstractSlice
 * Method:    disposeInternal
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_AbstractSlice_disposeInternal
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif