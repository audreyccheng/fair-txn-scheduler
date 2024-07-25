package shield.benchmarks.smallbank;

import java.io.IOException;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReadWriteLock;

import org.json.simple.parser.ParseException;
import shield.benchmarks.utils.CacheStats;
import shield.benchmarks.utils.ClientUtils;
import shield.benchmarks.utils.StatisticsCollector;
import shield.benchmarks.utils.TrxStats;
import shield.client.ClientTransaction;
import shield.client.DatabaseAbortException;
import shield.client.ClientBase;
// import shield.client.RedisPostgresClient;

/**
 * Simulates a client that runs a YCSB workload, the parameters of the YCSB workload are configured
 * in the json file passed in as argument
 *
 * @author ncrooks
 */
public class StartSmallBankTrxClient {

  public static class BenchmarkRunnable implements Runnable {

    public int threadNumber;
    public int clientId;
    public SmallBankExperimentConfiguration tpcConfig;
    public String expConfigFile;
    public HashMap<SmallBankConstants.Transactions, TrxStats> trxStats;
    public List<Double> tput = new LinkedList<>();
    // public Map<Long, ReadWriteLock> keyLocks;
    // public long prefetchMemoryUsage;

    BenchmarkRunnable(int threadNumber, int clientId, SmallBankExperimentConfiguration tpcConfig, String expConfigFile) { //, Map<Long, ReadWriteLock> keyLocks
      this.threadNumber = threadNumber;
      this.clientId = clientId;
      this.tpcConfig = tpcConfig;
      this.expConfigFile = expConfigFile;
      // this.keyLocks = keyLocks;
      // this.prefetchMemoryUsage = -1;
    }

    @Override
    public void run() {
      StatisticsCollector stats;
      SmallBankGenerator smallBankGenerator;
      long beginTime;
      long expiredTime;
      int nbExecuted = 0;
      int nbAbort = 0;
      boolean success = false;
      ClientTransaction ongoingTrx;
      int measurementKey = 0;
      boolean warmUp;
      boolean warmDown;
      long wbeginTime = 0;
      int prevCount = 1;

      stats = new StatisticsCollector(tpcConfig.RUN_NAME + "_thread" + this.threadNumber);

      try {
        ClientBase client = ClientUtils.createClient(tpcConfig.CLIENT_TYPE, expConfigFile); //, keyLocks, 7000 + this.threadNumber, this.threadNumber);
        // client.setThreadNumber(this.threadNumber);
        client.registerClient();

        System.out.println("Client registered");

        smallBankGenerator = new SmallBankGenerator(client, tpcConfig);

        System.out.println("Begin Client " + client.getBlockId() + System.currentTimeMillis());

        // Trying to minimise timing differences
        //Thread.sleep(client.getBlockId() * 500);

        beginTime = System.currentTimeMillis();
        expiredTime = 0;
        warmUp = true;
        warmDown = false;

        int totalRunningTime = (tpcConfig.EXP_LENGTH + tpcConfig.RAMP_UP + tpcConfig.RAMP_DOWN) * 1000;

        while (expiredTime < totalRunningTime) {
          if (!warmUp && !warmDown) {
            measurementKey = StatisticsCollector.addBegin(stats);
          }
          smallBankGenerator.runNextTransaction(this.clientId);
          if (!warmUp && !warmDown) {
            StatisticsCollector.addEnd(stats, measurementKey);
          }
          if (!warmUp && !warmDown && wbeginTime == 0) {
            wbeginTime = System.currentTimeMillis();
          }
          if (!warmUp && !warmDown && wbeginTime != 0) {
            nbExecuted++;
          }
          Long currTime = System.currentTimeMillis();
          if (!warmUp && !warmDown && (currTime - wbeginTime) % 1000 > prevCount) {
            tput.add((nbExecuted*1000.0)/(currTime - wbeginTime));
            prevCount++;
          }

          expiredTime = System.currentTimeMillis() - beginTime;
          if (expiredTime > tpcConfig.RAMP_UP * 1000)
            warmUp = false;
          if ((totalRunningTime - expiredTime) < tpcConfig.RAMP_DOWN * 1000)
            warmDown = true;

//          System.out.println("[Executed] " + nbExecuted + " " + expiredTime + " ");
        }

        // client.requestExecutor.shutdown();
        // while (!client.requestExecutor.isTerminated()) {}

        // smallBankGenerator.printStats();
        trxStats = smallBankGenerator.getTrxStats();
        // prefetchMemoryUsage = client.getPrefetchMapSize();
      } catch (Exception e) {
        e.printStackTrace();
      }
    }
  }

  public static void main(String[] args) throws InterruptedException,
      IOException, ParseException, DatabaseAbortException, SQLException {

    String expConfigFile;
    SmallBankExperimentConfiguration tpcConfig;

    if (args.length != 1) {
      System.err.println(
              "Incorrect number of arguments: expected <expConfigFile.json>");
    }
    // Contains the experiment paramaters
    expConfigFile = args[0];
    tpcConfig = new SmallBankExperimentConfiguration(expConfigFile);
    // Map<Long, ReadWriteLock> keyLocks = new ConcurrentHashMap<>(); // only make one lock map for all clients

    System.out.println("Number of loader threads " + tpcConfig.NB_LOADER_THREADS);

    if (tpcConfig.MUST_LOAD_KEYS) {
        System.out.println("Begin loading data");
        SmallBankLoader.loadData(tpcConfig, expConfigFile);
    }

    System.out.println("Data loaded");

    int[] client_breakdown = {tpcConfig.CLIENT1_THREADS, tpcConfig.NB_CLIENT_THREADS};
    int clientCount = 0;
    Thread[] threads = new Thread[tpcConfig.NB_CLIENT_THREADS];
    BenchmarkRunnable[] runnables = new BenchmarkRunnable[tpcConfig.NB_CLIENT_THREADS];
    for (int i = 0; i < tpcConfig.NB_CLIENT_THREADS; i++) {
      if (i >= client_breakdown[clientCount]) {
        clientCount++;
      }
      System.out.println("thread is client: " + (clientCount+1));
      runnables[i] = new BenchmarkRunnable(i, clientCount+1 /* clientId >= 1 */, tpcConfig, expConfigFile); //, keyLocks
      threads[i] = new Thread(runnables[i]);
      threads[i].start();
    }

    HashMap<SmallBankConstants.Transactions, TrxStats> combinedStats = new HashMap<>();
    List<Double> combinedTputs = new LinkedList<>();

    // long totalPrefetchMemoryUsage = 0;
    for (int i = 0; i < threads.length; i++) {
      threads[i].join();

      if (i == 0) {
        combinedTputs.addAll(runnables[i].tput);
      } else {
        for (int j = 0; j < combinedTputs.size() && j < runnables[i].tput.size(); j++) {
          combinedTputs.set(j, combinedTputs.get(j) + runnables[i].tput.get(j));
        }
      }

      HashMap<SmallBankConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
      threadStats.forEach((txn, stat) -> {
        if (!combinedStats.containsKey(txn)) combinedStats.put(txn, new TrxStats());
        combinedStats.get(txn).mergeTxnStats(stat);
      });

      // totalPrefetchMemoryUsage += runnables[i].prefetchMemoryUsage;
    }

    String res = "";
    for (Double d : combinedTputs) {
      res += Math.round(d) + ", ";
    }
    System.out.println("Len: " + combinedTputs.size());
    System.out.println("Tputs: " + res);

    // System.out.println("THREADS COMBINED STATS");

    // Over how long a period the statistics were taken from
    System.out.println("Benchmark duration: " + tpcConfig.EXP_LENGTH);
    combinedStats.forEach((tType,stat) -> System.out.println("[STAT] " + tType + " " +  stat.getStats()));

    List<Long> lats = new LinkedList<>();
    for (TrxStats ts : combinedStats.values()) {
      lats.addAll(ts.getLatencies());
      break;
    }
    String result = "";
    for (Long l : lats) {
      result += l + " ";
    }

    System.out.println();
    long txnsExecuted = combinedStats.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
    System.out.println("Average throughput: " + txnsExecuted / tpcConfig.EXP_LENGTH + " txn/s");
    System.out.println("Average latency: " + ((float) combinedStats.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted + "ms");
    long numAborts = combinedStats.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
    System.out.println("Total number of aborts: " + numAborts + " with abort rate: " + ((float) numAborts) / (numAborts + txnsExecuted));
    System.out.println("Lats: " + result);

    HashMap<SmallBankConstants.Transactions, TrxStats>[] clientMaps = new HashMap[tpcConfig.NUM_CLIENTS_FAIR];
    for (int i = 0; i < tpcConfig.NUM_CLIENTS_FAIR; i++) {
      clientMaps[i] = new HashMap<>();
    }

    clientCount = 0;
    for (int i = 0; i < threads.length; i++) {
      HashMap<SmallBankConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
      if (i >= client_breakdown[clientCount]) {
        clientCount++;
      }
      int finalClientCount = clientCount;
      threadStats.forEach((txn, stat) -> {
        if (!clientMaps[finalClientCount].containsKey(txn)) clientMaps[finalClientCount].put(txn, new TrxStats());
        clientMaps[finalClientCount].get(txn).mergeTxnStats(stat);
      });
    }

    int ccount = 1;
    for (Map<SmallBankConstants.Transactions, TrxStats> cmap : clientMaps) {
      System.out.println();
      long txnsExecuted1 = cmap.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
      System.out.println("Client " + ccount + " Average throughput: " + txnsExecuted1 / tpcConfig.EXP_LENGTH + " txn/s");
      System.out.println("Client " + ccount + " Average latency: " + ((float) cmap.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted1 + "ms");
      long numAborts1 = cmap.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
      System.out.println("Client " + ccount + "  Total number of aborts: " + numAborts1 + " with abort rate: " + ((float) numAborts1) / (numAborts1 + txnsExecuted1));
      ccount++;
    }

    HashMap<SmallBankConstants.Transactions, TrxStats> client1Stats = new HashMap<>();
    HashMap<SmallBankConstants.Transactions, TrxStats> client2Stats = new HashMap<>();

    for (int i = 0; i < threads.length; i++) {
      HashMap<SmallBankConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
      if (i < tpcConfig.CLIENT1_THREADS) {
        threadStats.forEach((txn, stat) -> {
          if (!client1Stats.containsKey(txn)) client1Stats.put(txn, new TrxStats());
          client1Stats.get(txn).mergeTxnStats(stat);
        });
      } else {
        threadStats.forEach((txn, stat) -> {
          if (!client2Stats.containsKey(txn)) client2Stats.put(txn, new TrxStats());
          client2Stats.get(txn).mergeTxnStats(stat);
        });
      }
    }

    System.out.println();
    long txnsExecuted1 = client1Stats.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
    System.out.println("Client 1 Average throughput: " + txnsExecuted1 / tpcConfig.EXP_LENGTH + " txn/s");
    System.out.println("Client 1 Average latency: " + ((float) client1Stats.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted1 + "ms");
    long numAborts1 = client1Stats.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
    System.out.println("Client 1 Total number of aborts: " + numAborts1 + " with abort rate: " + ((float) numAborts1) / (numAborts1 + txnsExecuted1));
    long txnsExecuted2 = client2Stats.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
    System.out.println("Client 2 Average throughput: " + txnsExecuted2 / tpcConfig.EXP_LENGTH + " txn/s");
    System.out.println("Client 2 Average latency: " + ((float) client2Stats.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted2 + "ms");
    long numAborts2 = client2Stats.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
    System.out.println("Client 2 Total number of aborts: " + numAborts + " with abort rate: " + ((float) numAborts2) / (numAborts2 + txnsExecuted2));

    System.out.println();
    // CacheStats.printReport();

    System.exit(0);
  }
}
