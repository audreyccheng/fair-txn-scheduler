/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_rocksdb_Statistics */

#ifndef _Included_org_rocksdb_Statistics
#define _Included_org_rocksdb_Statistics
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_rocksdb_Statistics
 * Method:    newStatistics
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_newStatistics__
  (JNIEnv *, jclass);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    newStatistics
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_newStatistics__J
  (JNIEnv *, jclass, jlong);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    newStatistics
 * Signature: ([B)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_newStatistics___3B
  (JNIEnv *, jclass, jbyteArray);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    newStatistics
 * Signature: ([BJ)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_newStatistics___3BJ
  (JNIEnv *, jclass, jbyteArray, jlong);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    disposeInternal
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_Statistics_disposeInternal
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    statsLevel
 * Signature: (J)B
 */
JNIEXPORT jbyte JNICALL Java_org_rocksdb_Statistics_statsLevel
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    setStatsLevel
 * Signature: (JB)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_Statistics_setStatsLevel
  (JNIEnv *, jobject, jlong, jbyte);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    getTickerCount
 * Signature: (JB)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_getTickerCount
  (JNIEnv *, jobject, jlong, jbyte);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    getAndResetTickerCount
 * Signature: (JB)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_Statistics_getAndResetTickerCount
  (JNIEnv *, jobject, jlong, jbyte);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    getHistogramData
 * Signature: (JB)Lorg/rocksdb/HistogramData;
 */
JNIEXPORT jobject JNICALL Java_org_rocksdb_Statistics_getHistogramData
  (JNIEnv *, jobject, jlong, jbyte);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    getHistogramString
 * Signature: (JB)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_Statistics_getHistogramString
  (JNIEnv *, jobject, jlong, jbyte);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    reset
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_Statistics_reset
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_Statistics
 * Method:    toString
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_rocksdb_Statistics_toString
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
