package shield.benchmarks.epinions;

import java.io.IOException;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Collections;
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

public class StartEpinionsTrxClient {

    public static class BenchmarkRunnable implements Runnable {

        public int threadNumber;
        public int clientId;
        public EpinionsExperimentConfiguration tpcConfig;
        public String expConfigFile;
        public HashMap<EpinionsConstants.Transactions, TrxStats> trxStats;
        public List<Double> tput = new LinkedList<>();
        // public Map<Long, ReadWriteLock> keyLocks;
        // public long prefetchMemoryUsage;

        BenchmarkRunnable(int threadNumber, int clientId, EpinionsExperimentConfiguration tpcConfig, String expConfigFile) { //, Map<Long, ReadWriteLock> keyLocks
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
            EpinionsGenerator epinionsGenerator;
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
                ClientBase client = ClientUtils.createClient(tpcConfig.CLIENT_TYPE, expConfigFile);
                // ClientUtils.createClient(tpcConfig.CLIENT_TYPE,
                //        expConfigFile, keyLocks, 7000 + this.threadNumber, this.threadNumber);
                // client.setThreadNumber(this.threadNumber);
                client.registerClient();

                System.out.println("Client registered");

                epinionsGenerator = new EpinionsGenerator(client, tpcConfig);

                System.out.println("Begin Client " + client.getBlockId() + System.currentTimeMillis());

                // Trying to minimise timing differences
                //Thread.sleep(client.getBlockId() * 500);

                beginTime = System.currentTimeMillis();
                expiredTime = 0;
                warmUp = true;
                warmDown = false;
                waitTime = true;

                int totalRunningTime = (tpcConfig.EXP_LENGTH + tpcConfig.RAMP_UP + tpcConfig.RAMP_DOWN) * 1000;

                while (expiredTime < totalRunningTime) {
                    if (!warmUp && !warmDown) {
                        measurementKey = StatisticsCollector.addBegin(stats);
                    }

                    if (expiredTime > tpcConfig.WAIT_TIME * 1000)
                        waitTime = false;

                    if (true) { //this.clientId != 1) { // TBU
                        epinionsGenerator.runNextTransaction(this.clientId);

//            if (!warmUp && !warmDown && wbeginTime == 0) {
//              wbeginTime = System.currentTimeMillis();
//            }
                        if (!warmUp && !warmDown) { //  && wbeginTime != 0
                            nbExecuted++;
                        }
                    } else {
                        if (!waitTime) {
                            epinionsGenerator.runNextTransaction(this.clientId);
//              if (!warmUp && !warmDown && wbeginTime == 0) {
//                wbeginTime = System.currentTimeMillis();
//              }
                            if (!warmUp && !warmDown) { //  && wbeginTime != 0
                                nbExecuted++;
                            }
                        } else {
                            Thread.sleep(100);
                        }
                    }

//                    epinionsGenerator.runNextTransaction();
                    if (!warmUp && !warmDown) {
                        StatisticsCollector.addEnd(stats, measurementKey);
                    }

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

//                    nbExecuted++;
                    expiredTime = System.currentTimeMillis() - beginTime;
                    if (expiredTime > tpcConfig.RAMP_UP * 1000)
                        warmUp = false;
                    if ((totalRunningTime - expiredTime) < tpcConfig.RAMP_DOWN * 1000)
                        warmDown = true;

//          System.out.println("[Executed] " + nbExecuted + " " + expiredTime + " ");
                }

                // client.requestExecutor.shutdown();
                // while (!client.requestExecutor.isTerminated()) {}

                // epinionsGenerator.printStats();
                trxStats = epinionsGenerator.getTrxStats();
                // prefetchMemoryUsage = client.getPrefetchMapSize();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    public static void main(String[] args) throws InterruptedException,
            IOException, ParseException, DatabaseAbortException, SQLException {

        String expConfigFile;
        EpinionsExperimentConfiguration tpcConfig;

        if (args.length != 1) {
            System.err.println(
                    "Incorrect number of arguments: expected <expConfigFile.json>");
        }
        // Contains the experiment paramaters
        expConfigFile = args[0];
        tpcConfig = new EpinionsExperimentConfiguration(expConfigFile);
        // Map<Long, ReadWriteLock> keyLocks = new ConcurrentHashMap<>(); // only make one lock map for all clients

        System.out.println("Number of loader threads " + tpcConfig.NB_LOADER_THREADS);

        if (tpcConfig.MUST_LOAD_KEYS) {
            System.out.println("Begin loading data");
            EpinionsLoader.loadData(tpcConfig, expConfigFile);
        }

        System.out.println("Data loaded");

        int[] client_breakdown = {tpcConfig.CLIENT1_THREADS, tpcConfig.NB_CLIENT_THREADS};
        int clientCount = 0;
        Thread[] threads = new Thread[tpcConfig.NB_CLIENT_THREADS];
        StartEpinionsTrxClient.BenchmarkRunnable[] runnables = new StartEpinionsTrxClient.BenchmarkRunnable[tpcConfig.NB_CLIENT_THREADS];
        for (int i = 0; i < tpcConfig.NB_CLIENT_THREADS; i++) {
            if (i >= client_breakdown[clientCount]) {
                clientCount++;
            }
            runnables[i] = new StartEpinionsTrxClient.BenchmarkRunnable(i, clientCount+1 /* clientId >= 1 */, tpcConfig, expConfigFile); //, keyLocks
            threads[i] = new Thread(runnables[i]);
            threads[i].start();
        }

        HashMap<EpinionsConstants.Transactions, TrxStats> combinedStats = new HashMap<>();
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

            HashMap<EpinionsConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
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

        // System.out.println("THREADS COMBINED STATS");

        // Over how long a period the statistics were taken from
        System.out.println("Benchmark duration: " + tpcConfig.EXP_LENGTH);
        combinedStats.forEach((tType,stat) -> System.out.println("[STAT] " + tType + " " +  stat.getStats()));
        // combinedStats.forEach((tType,stat) -> System.out.println("[STAT] " + tType + " " +  stat.getAvgLatency()));

      // System.out.print("Customers: ");
      // List<Integer> vals = new ArrayList<>(combinedCustStats.values());
      // Collections.sort(vals, Collections.reverseOrder());
      // for (Integer v : vals) {
      //     if (v > 1)
      //         System.out.print(v + " ");
      // }
      // System.out.println("");

      System.out.println();
      long txnsExecuted = combinedStats.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
      System.out.println("Average throughput: " + txnsExecuted / tpcConfig.EXP_LENGTH + " txn/s");
      double avg = ((float) combinedStats.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted;
      System.out.println("Average latency: " + avg + "ms");

      List<Long> lats = new LinkedList<>();
      for (TrxStats ts : combinedStats.values()) {
        lats.addAll(ts.getLatencies());
      }
      int index50 = (int) (.50 * lats.size());
      int index99 = Math.min((int) (.99 * lats.size()), lats.size() - 1);
      int index999 = Math.min((int) (.999 * lats.size()), lats.size() - 1);
      Collections.sort(lats);
      Object[] latsA = lats.toArray();

      double sumDiffsSquared = 0.0;
      for (Long value : lats) {
          double diff = value - avg;
          diff *= diff;
          sumDiffsSquared += diff;
      }
      double var = (sumDiffsSquared  / (lats.size()-1));
      System.out.println("Latency variance: " + var);

      System.out.println("50th percentile latency: " + latsA[index50]);
      System.out.println("99th percentile latency: " + latsA[index99]);
      System.out.println("99.9th percentile latency: " + latsA[index999]);
      System.out.println("Max latency: " + latsA[lats.size() - 1]);long numAborts = combinedStats.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
        System.out.println("Total number of aborts: " + numAborts + " with abort rate: " + ((float) numAborts) / (numAborts + txnsExecuted));
        //System.out.println("Prefetching memory usage: ~" + totalPrefetchMemoryUsage + " bytes");

        System.out.println();
        // CacheStats.printReport();

        HashMap<EpinionsConstants.Transactions, TrxStats>[] clientMaps = new HashMap[tpcConfig.NUM_CLIENTS_FAIR];
        for (int i = 0; i < tpcConfig.NUM_CLIENTS_FAIR; i++) {
            clientMaps[i] = new HashMap<>();
        }

        clientCount = 0;
        for (int i = 0; i < threads.length; i++) {
            HashMap<EpinionsConstants.Transactions, TrxStats> threadStats = runnables[i].trxStats;
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
        for (Map<EpinionsConstants.Transactions, TrxStats> cmap : clientMaps) {
            System.out.println();
            long txnsExecuted1 = cmap.values().stream().map(TrxStats::getExecuteCount).reduce(0L, Long::sum);
            System.out.println("Client " + ccount + " Average throughput: " + txnsExecuted1 / tpcConfig.EXP_LENGTH + " txn/s");
            System.out.println("Client " + ccount + " Average latency: " + ((float) cmap.values().stream().map(TrxStats::getTimeExecuted).reduce(0L, Long::sum)) / txnsExecuted1 + "ms");
            long numAborts1 = cmap.values().stream().map(TrxStats::getTotAborts).reduce(0L, Long::sum);
            System.out.println("Client " + ccount + "  Total number of aborts: " + numAborts1 + " with abort rate: " + ((float) numAborts1) / (numAborts1 + txnsExecuted1));
            ccount++;
        }

        System.exit(0);
    }
}
