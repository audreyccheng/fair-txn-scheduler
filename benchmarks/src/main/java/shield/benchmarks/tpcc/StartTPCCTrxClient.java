package shield.benchmarks.tpcc;

import java.io.IOException;
import java.sql.SQLException;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReadWriteLock;

import org.json.simple.parser.ParseException;
import shield.benchmarks.tpcc.utils.TPCCConstants;
import shield.benchmarks.tpcc.utils.TPCCConstants.Transactions;
import shield.benchmarks.utils.CacheStats;
import shield.benchmarks.utils.ClientUtils;
import shield.benchmarks.utils.StatisticsCollector;
import shield.benchmarks.utils.TrxStats;
import shield.client.ClientTransaction;
import shield.client.DatabaseAbortException;
import shield.client.ClientBase;
import shield.client.RedisPostgresClient;

/**
 * Simulates a client that runs a YCSB workload, the parameters of the YCSB workload are configured
 * in the json file passed in as argument
 *
 * @author ncrooks
 */
public class StartTPCCTrxClient {

    public static class BenchmarkRunnable implements Runnable {
        public int threadNumber;
        public int clientId;
        public TPCCExperimentConfiguration tpcConfig;
        public String expConfigFile;
        public HashMap<TPCCConstants.Transactions, TrxStats> trxStats;
        public Map<Long, ReadWriteLock> keyLocks;
        public long prefetchMemoryUsage;
        public List<Double> tput = new LinkedList<>();

        BenchmarkRunnable(int threadNumber, int clientId, TPCCExperimentConfiguration tpcConfig, String expConfigFile, Map<Long, ReadWriteLock> keyLocks) {
            this.threadNumber = threadNumber;
            this.clientId = clientId;
            this.tpcConfig = tpcConfig;
            this.expConfigFile = expConfigFile;
            this.keyLocks = keyLocks;
            this.prefetchMemoryUsage = -1;
        }

        @Override
        public void run() {
            StatisticsCollector stats;
            TPCCGenerator tpccGenerator;
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
            boolean waitTime;

            stats = new StatisticsCollector(tpcConfig.RUN_NAME + "_thread" + this.threadNumber);

            try {
                RedisPostgresClient client = (RedisPostgresClient) ClientUtils.createClient(tpcConfig.CLIENT_TYPE, expConfigFile, keyLocks, 7000 + this.threadNumber, this.threadNumber);
                client.setThreadNumber(this.threadNumber);
                client.registerClient();

                System.out.println("Client registered");

                tpccGenerator = new TPCCGenerator(client, tpcConfig);

                System.out.println("Begin Client " + client.getBlockId() + System.currentTimeMillis());

                beginTime = System.currentTimeMillis();
                expiredTime = 0;
                warmUp = true;
                warmDown = false;
                waitTime = true;

                Random ran = new Random();

                int totalRunningTIme = ((tpcConfig.EXP_LENGTH + tpcConfig.RAMP_DOWN + tpcConfig.RAMP_UP) * 1000);

                while (expiredTime < totalRunningTIme) {
//                    System.out.println(warmUp + " " + warmDown + " " + expiredTime);

                    if (tpcConfig.USE_THINK_TIME) Thread.sleep(ran.nextInt(tpcConfig.THINK_TIME));

                    if (!warmUp && !warmDown) {
                        measurementKey = StatisticsCollector.addBegin(stats);
                    }

                    if (expiredTime > tpcConfig.WAIT_TIME * 1000)
                        waitTime = false;

                    tpccGenerator.runNextTransaction(this.clientId);
                    if (!warmUp && !warmDown) {
                        StatisticsCollector.addEnd(stats, measurementKey);
                    }
                    if (!warmUp && !warmDown) { //  && wbeginTime != 0
                        nbExecuted++;
                    }
                    expiredTime = System.currentTimeMillis() - beginTime;
                    if (expiredTime > tpcConfig.RAMP_UP * 1000)
                        warmUp = false;
                    if ((totalRunningTIme - expiredTime) < (tpcConfig.RAMP_DOWN * 1000))
                        warmDown = true;

                    if (!warmUp && !warmDown && wbeginTime == 0) {
                        wbeginTime = System.currentTimeMillis();
                    }
                    Long currTime = System.currentTimeMillis();
                    if (!warmUp && !warmDown && (currTime - wbeginTime) % 5000 > prevCount) {
                        System.out.println("clientId: " + this.clientId + " nbExecuted: " + nbExecuted + " time: "
                                + (currTime - wbeginTime) + " tput: " + (nbExecuted*1000.0)/(currTime - wbeginTime));
                        tput.add((nbExecuted*1000.0)/(currTime - wbeginTime));
                        prevCount++;
                    }

//                    System.out.println("[Executed] " + nbExecuted + " " + expiredTime + " ");
                }

                client.requestExecutor.shutdown();
                while (!client.requestExecutor.isTerminated()) {
                }

                tpccGenerator.printStats();
                trxStats = tpccGenerator.getTrxStats();
                prefetchMemoryUsage = client.getPrefetchMapSize();
            } catch (Exception e) {}
        }
    }

  public static void main(String[] args) throws InterruptedException,
      IOException, ParseException, DatabaseAbortException, SQLException {

    String expConfigFile;
    TPCCExperimentConfiguration tpcConfig;

    if (args.length != 1) {
      System.err.println(
          "Incorrect number of arguments: expected <expConfigFile.json>");
    }
    // Contains the experiment paramaters
    expConfigFile = args[0];
    tpcConfig = new TPCCExperimentConfiguration(expConfigFile);
    Map<Long, ReadWriteLock> keyLocks = new ConcurrentHashMap<>(); // only make one lock map for all clients

      int[] client_breakdown = {tpcConfig.CLIENT1_THREADS, tpcConfig.NB_CLIENT_THREADS};
      int clientCount = 0;
     Thread[] threads = new Thread[tpcConfig.THREADS];
     BenchmarkRunnable[] runnables = new BenchmarkRunnable[tpcConfig.THREADS];
      for (int i = 0; i < tpcConfig.THREADS; i++) {
          if (i >= client_breakdown[clientCount]) {
              clientCount++;
          }
          runnables[i] = new  BenchmarkRunnable(i, clientCount+1 /* clientId >= 1 */, tpcConfig, expConfigFile, keyLocks);
          threads[i] = new Thread(runnables[i]);
          threads[i].start();
      }

      HashMap<TPCCConstants.Transactions, TrxStats> combinedStats = new HashMap<>();
      long totalPrefetchMemoryUsage = 0;
      List<Double> combinedTputs = new LinkedList<>();
      List<Double> combinedTputs1 = new LinkedList<>();
      List<Double> combinedTputs2 = new LinkedList<>();

      // long totalPrefetchMemoryUsage = 0;
      clientCount = 0;
      for (int i = 0; i < threads.length; i++) {
          threads[i].join();

          if (i >= client_breakdown[clientCount]) {
              if (combinedTputs2.size() == 0) {
                  combinedTputs2.addAll(runnables[i].tput);
              } else {
                  for (int j = 0; j < combinedTputs2.size() && j < runnables[i].tput.size(); j++) {
                      combinedTputs2.set(j, combinedTputs2.get(j) + runnables[i].tput.get(j));
                  }
              }
          } else {
              if (combinedTputs1.size() == 0) {
                  combinedTputs1.addAll(runnables[i].tput);
              } else {
                  for (int j = 0; j < combinedTputs1.size() && j < runnables[i].tput.size(); j++) {
                      combinedTputs1.set(j, combinedTputs1.get(j) + runnables[i].tput.get(j));
                  }
              }
          }

          if (i == 0) {
              combinedTputs.addAll(runnables[i].tput);
          } else {
              for (int j = 0; j < combinedTputs.size() && j < runnables[i].tput.size(); j++) {
                  combinedTputs.set(j, combinedTputs.get(j) + runnables[i].tput.get(j));
              }
          }

          HashMap<TPCCConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
          threadStats.forEach((txn, stat) -> {
              if (!combinedStats.containsKey(txn)) combinedStats.put(txn, new TrxStats());
              combinedStats.get(txn).mergeTxnStats(stat);
          });

          totalPrefetchMemoryUsage += runnables[i].prefetchMemoryUsage;
      }

      System.out.println("THREADS COMBINED STATS");

      String res = "";
      for (Double d : combinedTputs) {
          res += Math.round(d) + ", ";
      }
      System.out.println("Len: " + combinedTputs.size());
      System.out.println("[" + res + "]");

      res = "";
      for (Double d : combinedTputs1) {
          res += Math.round(d) + ", ";
      }
      System.out.println("[" + res + "]");
      res = "";
      for (Double d : combinedTputs2) {
          res += Math.round(d) + ", ";
      }
      System.out.println("[" + res + "]");

      // Over how long a period the statistics were taken from
      System.out.println("Benchmark duration: " + tpcConfig.EXP_LENGTH);
      combinedStats.forEach((tType,stat) -> System.out.println("[STAT] " + tType + " " +  stat.getStats()));

      System.out.println();
      long txnsExecuted = combinedStats.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
      System.out.println("Average throughput: " + txnsExecuted / tpcConfig.EXP_LENGTH + " txn/s");
      System.out.println("Average latency: " + ((float) combinedStats.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted + "ms");
      System.out.println("Prefetching memory usage ~" + totalPrefetchMemoryUsage + " bytes");

      HashMap<TPCCConstants.Transactions, TrxStats>[] clientMaps = new HashMap[tpcConfig.NUM_CLIENTS_FAIR];
      for (int i = 0; i < tpcConfig.NUM_CLIENTS_FAIR; i++) {
          clientMaps[i] = new HashMap<>();
      }

      clientCount = 0;
      for (int i = 0; i < threads.length; i++) {
          HashMap<TPCCConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
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
      for (Map<TPCCConstants.Transactions, TrxStats> cmap : clientMaps) {
          System.out.println();
          long txnsExecuted1 = cmap.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
          System.out.println("Client " + ccount + " Average throughput: " + txnsExecuted1 / tpcConfig.EXP_LENGTH + " txn/s");
          System.out.println("Client " + ccount + " Average latency: " + ((float) cmap.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted1 + "ms");
          long numAborts1 = cmap.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
          System.out.println("Client " + ccount + "  Total number of aborts: " + numAborts1 + " with abort rate: " + ((float) numAborts1) / (numAborts1 + txnsExecuted1));
          ccount++;
      }

      System.out.println();
      CacheStats.printReport();
      System.exit(0);
    }
}
