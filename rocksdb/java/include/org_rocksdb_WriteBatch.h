/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_rocksdb_WriteBatch */

#ifndef _Included_org_rocksdb_WriteBatch
#define _Included_org_rocksdb_WriteBatch
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    disposeInternal
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_disposeInternal
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    count0
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_org_rocksdb_WriteBatch_count0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    put
 * Signature: (J[BI[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_put__J_3BI_3BI
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    put
 * Signature: (J[BI[BIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_put__J_3BI_3BIJ
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    putDirect
 * Signature: (JLjava/nio/ByteBuffer;IILjava/nio/ByteBuffer;IIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_putDirect
  (JNIEnv *, jobject, jlong, jobject, jint, jint, jobject, jint, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    merge
 * Signature: (J[BI[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_merge__J_3BI_3BI
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    merge
 * Signature: (J[BI[BIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_merge__J_3BI_3BIJ
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    delete
 * Signature: (J[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_delete__J_3BI
  (JNIEnv *, jobject, jlong, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    delete
 * Signature: (J[BIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_delete__J_3BIJ
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    singleDelete
 * Signature: (J[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_singleDelete__J_3BI
  (JNIEnv *, jobject, jlong, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    singleDelete
 * Signature: (J[BIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_singleDelete__J_3BIJ
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    deleteDirect
 * Signature: (JLjava/nio/ByteBuffer;IIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_deleteDirect
  (JNIEnv *, jobject, jlong, jobject, jint, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    deleteRange
 * Signature: (J[BI[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_deleteRange__J_3BI_3BI
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    deleteRange
 * Signature: (J[BI[BIJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_deleteRange__J_3BI_3BIJ
  (JNIEnv *, jobject, jlong, jbyteArray, jint, jbyteArray, jint, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    putLogData
 * Signature: (J[BI)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_putLogData
  (JNIEnv *, jobject, jlong, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    clear0
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_clear0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    setSavePoint0
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_setSavePoint0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    rollbackToSavePoint0
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_rollbackToSavePoint0
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    popSavePoint
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_popSavePoint
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    setMaxBytes
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_setMaxBytes
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    newWriteBatch
 * Signature: (I)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_WriteBatch_newWriteBatch__I
  (JNIEnv *, jclass, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    newWriteBatch
 * Signature: ([BI)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_WriteBatch_newWriteBatch___3BI
  (JNIEnv *, jclass, jbyteArray, jint);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    iterate
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_iterate
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    data
 * Signature: (J)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_rocksdb_WriteBatch_data
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    getDataSize
 * Signature: (J)J
 */
JNIEXPORT jlong JNICALL Java_org_rocksdb_WriteBatch_getDataSize
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasPut
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasPut
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasDelete
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasDelete
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasSingleDelete
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasSingleDelete
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasDeleteRange
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasDeleteRange
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasMerge
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasMerge
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasBeginPrepare
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasBeginPrepare
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasEndPrepare
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasEndPrepare
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasCommit
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasCommit
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    hasRollback
 * Signature: (J)Z
 */
JNIEXPORT jboolean JNICALL Java_org_rocksdb_WriteBatch_hasRollback
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    markWalTerminationPoint
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_rocksdb_WriteBatch_markWalTerminationPoint
  (JNIEnv *, jobject, jlong);

/*
 * Class:     org_rocksdb_WriteBatch
 * Method:    getWalTerminationPoint
 * Signature: (J)Lorg/rocksdb/WriteBatch/SavePoint;
 */
JNIEXPORT jobject JNICALL Java_org_rocksdb_WriteBatch_getWalTerminationPoint
  (JNIEnv *, jobject, jlong);

#ifdef __cplusplus
}
#endif
#endif
