package shield.client;

import org.apache.commons.codec.DecoderException;
import org.apache.commons.codec.binary.Hex;
import org.json.simple.parser.ParseException;
import redis.clients.jedis.JedisPooled;
import shield.benchmarks.smallbank.StartSmallBankTrxClient;
import shield.benchmarks.taobench.StartTaoBenchTrxClient;
import shield.benchmarks.utils.CacheStats;
import shield.client.schema.Table;
import shield.network.messages.Msg.Message;
import shield.util.Pair;
import shield.util.Utility;

import java.io.IOException;
import java.sql.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.locks.*;
import java.util.stream.Collectors;

/**
 * Represents a client. Interfaces to library code. Clients are single threaded and can only have
 * one ongoing transaction at a time.
 *
 * TODO(Natacha): Code needs to be refactored to reduce code duplication
 *
 * @author ncrooks
 */
public final class RedisPostgresClient extends ClientBase {

  // TODO: Make this config parameters
  private static final double PREFETCH_FREQ_THRESH = 0.2;
  private static final double PREFETCH_LEN_THRES = 5;

  private class PrefetchTracker {
    List<PrefetchSet> sets;
    int totalFreq;
    boolean trackDeps;

    PrefetchTracker() {
      this.sets = new LinkedList<>();
      this.totalFreq = 0;
      this.trackDeps = true;
    }

    void observeSubsequentLayer(Set<Pair<String, Long>> keys) {
      this.totalFreq++;
      for (PrefetchSet set : this.sets) {
        if (set.keys.equals(keys)) {
          set.freq++;
          this.sets.sort((o1, o2) -> Long.compare(o2.freq, o1.freq));
          return;
        }
      }

      // Set didn't exist before
      this.sets.add(new PrefetchSet(keys));
      this.sets.sort((o1, o2) -> Long.compare(o2.freq, o1.freq));

      // Enforce maximum number of sets
      if (sets.size() > PREFETCH_LEN_THRES) {
        this.trackDeps = false;
      }
    }

    public long getSize() {
      long size = 8; // Assuming int and boolean both 4 bytes each
      for (PrefetchSet s : this.sets) {
        size += s.getSize();
      }
      return size;
    }
  }

  private class PrefetchSet {
    Set<Pair<String, Long>> keys;
    int freq;

    PrefetchSet(Set<Pair<String, Long>> keys) {
      this.keys = keys;
      this.freq = 1;
    }

    public long getSize() {
      // Assuming int is 4 bytes
      long size = 4;
      for (Pair<String, Long> key : keys) {
        size += 8 + key.getLeft().length();
      }
      return size;
    }
  }

  // Txn lock timeout from 500ms to 2000ms
  private static int getRandomTimeout() {
    return (int) (Math.random() * 1500) + 500;
  }

  /**
   * Stores a list of tables that have already been created
   */
  private Set<String> tableNames;

  /**
   * Postgres Connection
   *
   */
  private Connection connection;

  /**
   * Redis Connection
   */
  private JedisPooled jedis;

  /**
   * Prefetching map. Maps txn_type:id -> PrefetchTracker
   */
  private Map<String, PrefetchTracker> prefetchMap;

  /**
   * Maps txn_id -> sets of IDs.
   */
  private Map<Long, Set<Long>> lastLayerMap;

  /**
   * Maps txn_id -> last set of prefetched keys, and whether or not they were already in the cache
   */
  private Map<Long, Map<Long, Boolean>> lastLayerPrefetched;

  /**
   * List of statements that have not yet been executed
   */
  private LinkedList<Pair<PreparedStatement, RedisStatement>> pendingStatements
      = new LinkedList<>();

  private Map<Long, ReadWriteLock> keyLocks;
  private Set<Long> readLockIds = new HashSet<>();
  private Set<Long> writeLockIds = new HashSet<>();
  private int threadNumber;

  public ThreadPoolExecutor requestExecutor;

  public void setThreadNumber(int threadNumber) {
    this.threadNumber = threadNumber;
  }

  public RedisPostgresClient() throws InterruptedException, IOException, ParseException, SQLException {
    super();
    this.threadNumber = -1;
    initClient();
  }

  public RedisPostgresClient(String configFileName)
          throws InterruptedException, ParseException, IOException, SQLException {
    super(configFileName);
    initClient();
  }

  public RedisPostgresClient(String configFileName, Map<Long, ReadWriteLock> keyLocks, int port, int uid)
          throws InterruptedException, ParseException, IOException, SQLException {
    super(configFileName, port, uid);
    this.keyLocks = keyLocks;
    initClient();
  }

  public RedisPostgresClient(String configFileName, int port, int uid)
      throws InterruptedException, ParseException, IOException, SQLException {
    super(configFileName, port, uid);
    initClient();
  }

  public RedisPostgresClient(String configFileName, String address, int port, int uid)
      throws InterruptedException, ParseException, IOException, SQLException {
    super(configFileName, address, port, uid);
    initClient();
  }

  private void initClient() {
    System.out.println("Initialising Clients");

    this.lastLayerMap = new HashMap<>();
    this.lastLayerPrefetched = new HashMap<>();
    this.prefetchMap = new HashMap<>();

    this.requestExecutor = (ThreadPoolExecutor) Executors.newFixedThreadPool(config.REQ_THREADS_PER_BM_THREAD);

    tableNames = new HashSet<String>();
    String jdbcUrl = "jdbc:postgresql://" + config.POSTGRES_HOSTNAME + ":" +
        config.POSTGRES_PORT + "/" + config.POSTGRES_DB_NAME + "?user=" + config.POSTGRES_USERNAME + "&password="
        + config.POSTGRES_PASSWORD;
    // Load the JDBC driver
    try {
      System.out.println("Loading driver...");
      Class.forName("org.postgresql.Driver");
      System.out.println("Driver loaded!");
      System.out.println("Connection " + jdbcUrl);
    } catch (ClassNotFoundException e) {
      throw new RuntimeException("Cannot find the driver in the classpath!", e);
    }
    try {
      connection = DriverManager.getConnection(jdbcUrl);
    } catch (SQLException e) {
      System.err.println(jdbcUrl);
      System.err.println(e);
      System.err.println(e.getErrorCode());
      System.err.println(e.getMessage());
      System.exit(-1);
    }

    try {
      this.jedis = new JedisPooled(config.REDIS_HOSTNAME, Integer.parseInt(config.REDIS_PORT));
    } catch (Exception e) {
      System.err.println(e);
      System.exit(-1);
    }

    databaseSchema = new HashMap<String, Table>();
  }


  private void createTable(String tableName) {
//    System.out.println("CREATING TABLE " + tableName);
    String createTable = (
        "CREATE TABLE " + tableName +
            "(id BIGINT not null, " +
            "data VARCHAR," +
            "PRIMARY KEY (id))");
//    System.out.println(createTable);
    tableNames.add(tableName);
    try {
      java.sql.Statement stmt = connection.createStatement();
      stmt.executeUpdate(createTable);
      connection.commit();
    } catch (SQLException e) {
      System.err.println(e.getMessage());
    }
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#registerClient()
   */
  @Override
  public synchronized void registerClient() throws DatabaseAbortException {
    assert this.threadNumber >= 0;
    try {
      connection.setAutoCommit(false);
      // connection.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ);
      // connection.setAutoCommit(false);
      connection.commit();
    } catch (SQLException e) {
      e.printStackTrace();
      System.exit(-1);
    }

//    createTable(config.RDS_TABLE_NAME);
  }


  public void startTransaction() throws DatabaseAbortException {
  }

  public void scheduleTransaction(int type) throws DatabaseAbortException {
  }
  public void scheduleTransactionFair(int type, int appId) throws DatabaseAbortException {
  }

  @Override
  public List<byte[]> readAndExecute(String table, String key) throws DatabaseAbortException {
    return null;
  }

  public void createReadStatement(String table, String key, int txn_type, long txn_id) throws SQLException {

    if (!tableNames.contains(table)) {
      // Table may not have been created yet, so create it
      createTable(table);
    }

    String query = "SELECT data FROM " + table + " WHERE id = ?";
    String row = table + key;
    Long id = Utility.hashPersistent(row);

    PreparedStatement prepStatement = connection.prepareStatement(query);
    prepStatement.setLong(1, id);

    RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.GET, table, id, null, txn_type, txn_id);

    pendingStatements.add(new Pair<>(prepStatement, redisStatement));
  }

  public void createUpdateStatement(String table, String key, byte[] value, int txn_type, long txn_id) throws SQLException {
    if (!tableNames.contains(table)) {
      // Table may not have been created yet, so create it
      createTable(table);
    }
    String row = table + key;
    Long id = Utility.hashPersistent(row);
    String update = " UPDATE " +
        table +
        " SET data=? where id=?";
    PreparedStatement preparedStatement = connection.prepareStatement(update);
    preparedStatement.setBytes(1, value);
    preparedStatement.setLong(2, id);
//      preparedStatement.closeOnCompletion();

    RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.PUT, table, id, value, txn_type, txn_id);

    pendingStatements.add(new Pair<>(preparedStatement, redisStatement));

  }

  public void createWriteStatement(String table, String key, byte[] value, int txn_type, long txn_id) throws SQLException {
    if (!tableNames.contains(table)) {
      // Table may not have been created yet, so create it
      createTable(table);
    }
    String row = table + key;
    Long id = Utility.hashPersistent(row);
    String update = "INSERT INTO  " +
        table +
        "(id, data) VALUES(?,?) ON CONFLICT (id) DO UPDATE SET data=?";
    PreparedStatement preparedStatement = connection.prepareStatement(update);
    preparedStatement.setLong(1, id);
    preparedStatement.setBytes(2, value);
    preparedStatement.setBytes(3, value);
//      preparedStatement.closeOnCompletion();

    RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.PUT, table, id, value, txn_type, txn_id);

    pendingStatements.add(new Pair<>(preparedStatement, redisStatement));
  }

  public void createDeleteStatement(String table, String key, int txn_type, long txn_id) throws SQLException {
    if (!tableNames.contains(table)) {
      // Table may not have been created yet, so create it
      createTable(table);
    }
    String query = "DELETE  FROM " + table + " WHERE id = ?";
    String row = table + key;
    Long id = Utility.hashPersistent(row);
    PreparedStatement prepStatement = connection.prepareStatement(query);
    //     prepStatement.closeOnCompletion();
    prepStatement.setLong(1, id);

    RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.DELETE, table, id, null, txn_type, txn_id);

    pendingStatements.add(new Pair<>(prepStatement, redisStatement));
  }

  public void createReadForUpdateStatement(String table, String key, int txn_type, long txn_id) throws SQLException {
    if (!tableNames.contains(table)) {
      // Table may not have been created yet, so create it
      createTable(table);
    }
    String query = "SELECT data FROM " + table + " WHERE id = ? FOR UPDATE";
    String row = table + key;
    Long id = Utility.hashPersistent(row);
    PreparedStatement prepStatement = connection.prepareStatement(query);
    //    prepStatement.closeOnCompletion();
    prepStatement.setLong(1, id);

    RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.GET, table, id, null, txn_type, txn_id);

    pendingStatements.add(new Pair<>(prepStatement, redisStatement));
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#readAndExecute(java.lang.String)
   */

  public synchronized List<byte[]> readAndExecute(String table, String row, int txn_type, long txn_id)
      throws DatabaseAbortException {

    try {
      // Generate and send any non-executed statements
      createReadStatement(table, row, txn_type, txn_id);
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  @Override
  public void read(String table, String key) throws DatabaseAbortException {
    read(table, key, -1, -1);
  }

  public void reset() {
    pendingStatements.clear();
    pendingStatements.clear();
    pendingStatements.clear();
    pendingStatements.clear();
  }

  public void handleError(SQLException e)
      throws DatabaseAbortException {
    // Return read values
    try {
      connection.rollback();
      e.printStackTrace();
      reset();
    } catch (SQLException e1) {
      //TODO(natacha) handle property
      e1.printStackTrace();
      ;
    }
    throw new DatabaseAbortException();

  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#read(java.lang.String)
   */
  public synchronized void read(String tableName, String row, int txn_type, long txn_id) throws DatabaseAbortException {
    try {
      createReadStatement(tableName, row, txn_type, txn_id);
    } catch (SQLException e) {
      handleError(e);
    }
  }

  @Override
  public synchronized List<byte[]> execute() throws DatabaseAbortException {
    try {
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  @Override
  public List<byte[]> readForUpdateAndExecute(String table, String key) throws DatabaseAbortException {
    return readForUpdateAndExecute(table, key, -1, -1);
  }

  public synchronized List<byte[]> readForUpdateAndExecute(String table, String row, int txn_type, long txn_id)
      throws DatabaseAbortException {
    try {

      createReadForUpdateStatement(table, row, txn_type, txn_id);
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  @Override
  public void readForUpdate(String table, String key) throws DatabaseAbortException {
    readForUpdate(table, key, -1, -1);
  }

  public synchronized void readForUpdate(String tableName, String row, int txn_type, long txn_id)
      throws DatabaseAbortException {
    try {
      createReadForUpdateStatement(tableName, row, txn_type, txn_id);
    } catch (SQLException e) {
      handleError(e);
    }
  }

  @Override
  public void delete(String table, String key) throws DatabaseAbortException {
    delete(table, key, -1, -1);
  }

  public synchronized void delete(String tableName, String row, int txn_type, long txn_id) throws DatabaseAbortException {
    try {
      createDeleteStatement(tableName, row, txn_type, txn_id);
    } catch (SQLException e) {
      handleError(e);
    }
  }

  @Override
  public List<byte[]> deleteAndExecute(String table, String key) throws DatabaseAbortException {
    return deleteAndExecute(table, key, -1, -1);
  }

  public synchronized List<byte[]> deleteAndExecute(String table, String row, int txn_type, long txn_id)
      throws DatabaseAbortException {
    try {

      createDeleteStatement(table, row, txn_type, txn_id);
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  @Override
  public void write(String table, String key, byte[] value) throws DatabaseAbortException {
    write(table, key, value, -1, -1);
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#write(java.lang.String, byte[])
   */
  public void write(String table, String row, byte[] value, int txn_type, long txn_id) throws DatabaseAbortException {
    try {
      createWriteStatement(table, row, value, txn_type, txn_id);
    } catch (SQLException e) {
      handleError(e);
    }
  }

  @Override
  public void update(String table, String key, byte[] value) throws DatabaseAbortException {
    update(table, key, value, -1, -1);
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#update(java.lang.String, byte[])
   */
  public void update(String table, String row, byte[] value, int txn_type, long txn_id) throws DatabaseAbortException {
    try {
      createUpdateStatement(table, row, value, txn_type, txn_id);
    } catch (SQLException e) {
      handleError(e);
    }
  }

  @Override
  public List<byte[]> writeAndExecute(String table, String key, byte[] value) throws DatabaseAbortException {
    return writeAndExecute(table, key, value, -1, -1);
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#updateAndExecute(java.lang.String, byte[])
   */
  public synchronized List<byte[]> updateAndExecute(String table,
      String row, byte[] value, int txn_type, long txn_id)
      throws DatabaseAbortException {

    try {
      createUpdateStatement(table, row, value, txn_type, txn_id);
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#writeAndExecute(java.lang.String, byte[])
   */
  public synchronized List<byte[]> writeAndExecute(String table,
      String row, byte[] value, int txn_type, long txn_id)
      throws DatabaseAbortException {

    try {
      createWriteStatement(table, row, value, txn_type, txn_id);
      List<byte[]> results = executeOps();
      return results;
    } catch (SQLException e) {
      handleError(e);
    }
    return null;
  }

  @Override
  public List<byte[]> updateAndExecute(String table, String key, byte[] value) throws DatabaseAbortException {
    return updateAndExecute(table, key, value, -1, -1);
  }


  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#commitTransaction()
   */
  @Override
  public synchronized List<byte[]> commitTransaction()
      throws DatabaseAbortException {
    try {
      List<byte[]> results = executeOps();
      connection.commit();

      if (config.REDIS_ENABLED) {
        for (Long id : readLockIds) {
//          System.out.printf("[TXN PROJ %d] READ UNLOCK %d\n", this.threadNumber, id);
          keyLocks.get(id).readLock().unlock();
        }
        readLockIds.clear();
        for (Long id : writeLockIds) {
//          System.out.printf("[TXN PROJ %d] WRITE UNLOCK %d\n", this.threadNumber, id);
          keyLocks.get(id).writeLock().unlock();
        }
        writeLockIds.clear();
      }

      return results;
    } catch (SQLException e) {
      handleError(e);
    }

    return null;
  }

  /*
   * (non-Javadoc)
   *
   * @see shield.client.ClientBase#abortTransaction()
   */
  @Override
  public synchronized void abortTransaction() throws DatabaseAbortException {
    try {
      pendingStatements.clear();
      connection.rollback();
    } catch (SQLException e) {
      handleError(e);
    }

    if (config.REDIS_ENABLED) {
      for (Long id : readLockIds) {
//        System.out.printf("[TXN PROJ %d] READ UNLOCK %d\n", this.threadNumber, id);
        keyLocks.get(id).readLock().unlock();
      }
      readLockIds.clear();
      for (Long id : writeLockIds) {
//        System.out.printf("[TXN PROJ %d] WRITE UNLOCK %d\n", this.threadNumber, id);
        keyLocks.get(id).writeLock().unlock();
      }
      writeLockIds.clear();
    }
  }

  private class ExecuteOpRunnable implements Runnable {
    private volatile PreparedStatement stat;
    private volatile RedisStatement redisStatement;

    private volatile boolean spedUp;
    private volatile byte[] results;
    private volatile SQLException sqlException;
    private volatile boolean done;

    ExecuteOpRunnable(PreparedStatement stat, RedisStatement redisStatement) {
      this.stat = stat;
      this.redisStatement = redisStatement;

      this.spedUp = false;
      this.results = null;
      this.sqlException = null;
      this.done = false;
    }

    public byte[] getResults() {
      return results;
    }

    public SQLException getSqlException() {
      return sqlException;
    }

    @Override
    public void run() {
      if (config.REDIS_ENABLED) {
        long start = System.currentTimeMillis();
        byte[] redisResult = redisStatement.execute(jedis);

        // Measure effective redis access time
        // Don't want to be overwhelmed with text, take roughly 1 in 1000 samples
//        if (start % 1000 == 0) System.out.println("redis access time: " + (System.currentTimeMillis() - start));

        if (redisResult != null) {
          // Result is not null ONLY when reading a value that Redis has
          // If reading a value that Redis doesn't have, we have to go to SQL
          // If writing, we write back to SQL too
          this.results = redisResult;
          this.spedUp = true;
          this.done = true;
          return;
        }
      }

      try {
//        if (config.LATENCY > 0) {
//          Thread.sleep(config.LATENCY);
//        }
        if (stat.execute()) {
          byte[] current;
          try (ResultSet resultSet = stat.getResultSet()) {
            if (resultSet.next()) {
              current = Hex.decodeHex(resultSet.getString(1).substring(2).toCharArray());
            } else {
              current = new byte[0];
            }

            // If read and Redis miss, add to redis
            if (config.REDIS_ENABLED && redisStatement.type == RedisStatement.RedisRequestType.GET) {
              jedis.set(Long.toString(redisStatement.id).getBytes(), current);
            }
          }
          this.results = current;
        }
        stat.close();
        this.done = true;
      } catch (SQLException e) {
        sqlException = e;
        this.done = true;
      } catch (DecoderException e) {
        this.done = true;
      }
//      catch (InterruptedException e) {
//        e.printStackTrace();
//      }
    }
  }

  /**
   * Sends any non-executed statement. Assume that the calling function is calling semaphore.wait()
   * to wait for replies.
   *
   * NB: sending multiple operations at once will allow the operations to be executed concurrently
   * at the server. This means, however, that we cannot send more than one operation with the same
   * key per batch of operations We check this here (AK: only non conflicting operations can be
   * sent).
   */
  public synchronized LinkedList<byte[]> executeOps() throws DatabaseAbortException, SQLException {

    LinkedList<byte[]> readResults = new LinkedList<byte[]>();
    List<ExecuteOpRunnable> opRunnables = new LinkedList<>();

    if (pendingStatements.size() <= 0) return readResults;

    long txn_id = pendingStatements.get(0).getRight().txn_id;

    int prefetchesUsed = 0;
    int redundantPrefetchesUsed = 0;
    if (config.REDIS_ENABLED && config.REDIS_PREFETCH) {
      // Update the prefetch data structure for the keys in the previous layer for this txn, if exists
      int txn_type = pendingStatements.get(0).getRight().txn_type;
      Set<Long> lastLayer = lastLayerMap.get(txn_id);
      Set<Pair<String, Long>> thisLayerWithTables = pendingStatements.stream().map(cs -> new Pair<String, Long>(cs.getRight().table, cs.getRight().id)).collect(Collectors.toSet());
      Set<Long> thisLayer = pendingStatements.stream().map(cs -> cs.getRight().id).collect(Collectors.toSet());
//      System.out.printf("[TXN PROJ %d] txn_id: %d; last_layer: %s; this_layer: %s\n", this.threadNumber, txn_id, lastLayer == null ? "NULL" : lastLayer.toString(), thisLayer.toString());

      if (lastLayer != null) {
        Map<Long, Boolean> prefetchedLastLayer = lastLayerPrefetched.get(txn_id);

        // Determine how many of the current layer's requests would hit due to a prefetch
        for (long currentLayerId : pendingStatements.stream().map(cs -> cs.getRight().id).collect(Collectors.toList())) {
          // Track if this req. is a hit due to a prefetch last layer
          if (prefetchedLastLayer.containsKey(currentLayerId)) {
            prefetchesUsed++;
            if (prefetchedLastLayer.get(currentLayerId)) redundantPrefetchesUsed++;
          }
        }

        for (long lastLayerId : lastLayer) {
          String prefetchIndex = txn_type + ":" + lastLayerId;
          PrefetchTracker tracker = prefetchMap.get(prefetchIndex);

          if (tracker == null) {
            tracker = new PrefetchTracker();
            prefetchMap.put(prefetchIndex, tracker);
          }

          if (tracker.trackDeps) {
            tracker.observeSubsequentLayer(thisLayerWithTables);
          }
        }
      }
      lastLayerMap.put(txn_id, thisLayer);

      // Determine what keys to prefetch
      Set<Pair<String, Long>> prefetchKeys = new HashSet<>();
      for (Pair<PreparedStatement, RedisStatement> combinedStatement : pendingStatements) {
        Long id = combinedStatement.getRight().id;
        int type = combinedStatement.getRight().txn_type;
        String prefetchIndex = type + ":" + id;

        PrefetchTracker tracker = prefetchMap.get(prefetchIndex);
//        System.out.printf("[TXN PROJ %d] %s prefetch object: %s\n", this.threadNumber, prefetchIndex, tracker == null ? "NULL" : tracker.toString());
        if (tracker != null && tracker.trackDeps && tracker.sets.size() > 0 && ((double) tracker.sets.get(0).freq) / tracker.totalFreq > PREFETCH_FREQ_THRESH)
          prefetchKeys.addAll(tracker.sets.get(0).keys);
      }

//      System.out.printf("[TXN PROJ %d] Prefetch: %s\n", this.threadNumber, prefetchKeys.stream().map(k -> Long.toString(k.getRight())).collect(Collectors.toSet()));

      // Remember which keys we prefetched for this txn
      Map<Long, Boolean> thisLayerPrefetched = new HashMap<>();

      // Add all keys to prefetch as reads
      for (Pair<String, Long> key : prefetchKeys) {
        String table = key.getLeft();
        long id = key.getRight();
        thisLayerPrefetched.put(id, false);

        String query = "SELECT data FROM " + table + " WHERE id = ?";

        PreparedStatement prepStatement = connection.prepareStatement(query);
        prepStatement.setLong(1, id);

        RedisStatement redisStatement = new RedisStatement(RedisStatement.RedisRequestType.GET, table, id, null, -1, -1, true);

        pendingStatements.add(new Pair<>(prepStatement, redisStatement));
      }

      lastLayerPrefetched.put(txn_id, thisLayerPrefetched);
    }

    for (Pair<PreparedStatement, RedisStatement> combinedStatement : pendingStatements) {
      Long id = combinedStatement.getRight().id;

//      System.out.printf("[TXN PROJ %d] processing %d\n", this.threadNumber, id);
      if (config.REDIS_ENABLED && !combinedStatement.getRight().prefetch) {
        keyLocks.computeIfAbsent(id, key -> new ReentrantReadWriteLock());
        if (combinedStatement.getRight().type == RedisStatement.RedisRequestType.GET) {
//          System.out.printf("[TXN PROJ %d] READING %d\n", this.threadNumber, id);
          if (!readLockIds.contains(id)) {
            try {
//              System.out.printf("[TXN PROJ %d] READ LOCK %d\n", this.threadNumber, id);
              if (!keyLocks.get(id).readLock().tryLock(getRandomTimeout(), TimeUnit.MILLISECONDS)) {
                this.abortTransaction();
                throw new DatabaseAbortException("Failed to acquire lock");
              }
            } catch (InterruptedException e) {}

            readLockIds.add(id);
          }
        } else {
//          System.out.printf("[TXN PROJ %d] WRITING %d\n", this.threadNumber, id);
          // Unlock read lock first if holding it
          if (readLockIds.contains(id)) {
//            System.out.printf("[TXN PROJ %d] READ UNLOCK %d\n", this.threadNumber, id);
            keyLocks.get(id).readLock().unlock();
            readLockIds.remove(id);
          }
          if (!writeLockIds.contains(id)) {
            try {
//              System.out.printf("[TXN PROJ %d] WRITE LOCK %d\n", this.threadNumber, id);
              if (!keyLocks.get(id).writeLock().tryLock(getRandomTimeout(), TimeUnit.MILLISECONDS)) {
                this.abortTransaction();
                throw new DatabaseAbortException("Failed to acquire lock");
              }
            } catch (InterruptedException e) {}

            writeLockIds.add(id);
          }
        }
      }

      ExecuteOpRunnable opRunnable = new ExecuteOpRunnable(combinedStatement.getLeft(), combinedStatement.getRight());
      this.requestExecutor.execute(opRunnable);

      opRunnables.add(opRunnable);
    }

    int numRequestsSpedUp = 0;

    List<byte[]> layerIds = new LinkedList<>();

    for (ExecuteOpRunnable r : opRunnables) {
      // Don't wait for or track stats for prefetches
//      if (r.redisStatement.prefetch) continue;

      // Wait for request to finish
      while (!r.done) {}

      // Don't track stats for prefetches
      if (r.redisStatement.prefetch) {
        if (r.spedUp) {
          lastLayerPrefetched.get(txn_id).put(r.redisStatement.id, true);
        }
        continue;
      }

      layerIds.add(Long.toString(r.redisStatement.id).getBytes());

      // If thread threw SQL exception inside, throw it here
      SQLException e = r.getSqlException();
      if (e != null) throw e;

      // Track speedup and add to results
      if (r.spedUp) numRequestsSpedUp++;
      if (r.getResults() != null)
        readResults.add(r.getResults());
    }

    // Run MGET on this layer (excepting prefetches) to update score
    if (config.REDIS_ENABLED) {
      jedis.mget(layerIds.toArray(new byte[0][]));
    }

    CacheStats.ranLayer(
            numRequestsSpedUp,
            (int) opRunnables.stream().filter(r -> !r.redisStatement.prefetch).count(),
            (int) opRunnables.stream().filter(r -> !r.redisStatement.prefetch && r.redisStatement.type == RedisStatement.RedisRequestType.GET).count(),
            (int) opRunnables.stream().filter(r -> r.redisStatement.prefetch).count(),
            (int) opRunnables.stream().filter(r -> r.redisStatement.prefetch && r.spedUp).count(),
            prefetchesUsed,
            redundantPrefetchesUsed
    );
    pendingStatements.clear();

    return readResults;
  }

  public long getPrefetchMapSize() {
    long size = 0;
    for (Map.Entry<String, PrefetchTracker> prefetchMapEntry : this.prefetchMap.entrySet()) {
      size += prefetchMapEntry.getKey().length();
      size += prefetchMapEntry.getValue().getSize();
    }
    return size;
  }

  /**
   * Executes a pre-generated transaction (the transaction already contains statements)
   *
   * @param trx - the transaction
   */
  public List<byte[]> executeTransaction(ClientTransaction trx)
      throws DatabaseAbortException {
    throw new RuntimeException("Unimplemented");
  }


  /*
   * (non-Javadoc)
   *
   * @see shield.BaseNode#handleMsg(shield.network.messages.Msg.Message)
   */
  @Override
  public void handleMsg(Message msg) {
    throw new RuntimeException("Unimplemented");
  }


}
