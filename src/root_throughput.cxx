/* Copyright (C) 2020 Enrico Guiraud
   See the LICENSE file in the top directory for more information. */

#include <TBranch.h>
#include <TFile.h>
#include <TStopwatch.h>
#include <TTree.h>

#include <algorithm>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Data {
   /// Either a single tree name common for all files, or one tree name per file.
   std::vector<std::string> fTreeNames;
   /// List of input files.
   // TODO add support for globbing
   std::vector<std::string> fFileNames;
   /// Branches to read.
   std::vector<std::string> fBranchNames;
};

struct Result {
   /// Elapsed real time spent reading and decompressing all data, in seconds.
   double fRealTime;
   double fCpuTime;
   // TODO returning zipped bytes read too might be interesting, e.g. to estimate network I/O speed
   ULong64_t fUncompressedBytesRead;
};

ULong64_t ReadTree(const std::string &treeName, const std::string &fileName, const std::vector<std::string> &branchNames)
{
   TFile f(fileName.c_str());
   if (f.IsZombie())
      throw std::runtime_error("There was a problem opening file \"" + fileName + '"');
   auto *t = f.Get<TTree>(treeName.c_str());
   if (t == nullptr)
      throw std::runtime_error("There was a problem retrieving TTree \"" + treeName + "\" from file \"" + fileName +
                               '"');
   t->SetBranchStatus("*", 0);
   std::vector<TBranch *> branches(branchNames.size());
   auto getBranch = [t](const std::string &bName) {
      auto *b = t->GetBranch(bName.c_str());
      if (b == nullptr)
         throw std::runtime_error("There was a problem retrieving TBranch \"" + bName + "\" from TTree \"" +
                                  t->GetName() + "\" in file \"" + t->GetCurrentFile()->GetName() + '"');
      b->SetStatus(1);
      return b;
   };
   std::transform(branchNames.begin(), branchNames.end(), branches.begin(), getBranch);

   const auto nEntries = t->GetEntries();
   ULong64_t bytesRead = 0ull;
   for (auto e = 0ll; e < nEntries; ++e)
      for (auto b : branches)
         bytesRead += b->GetEntry(e);

   return bytesRead;
}

Result EvalThroughputST(const Data &d)
{
   TStopwatch sw;
   sw.Start();

   auto treeIdx = 0;
   ULong64_t bytesRead = 0ull;
   for (const auto &fName : d.fFileNames) {
      bytesRead += ReadTree(d.fTreeNames[treeIdx], fName, d.fBranchNames);
      if (d.fTreeNames.size() > 1)
         ++treeIdx;
   }

   sw.Stop();

   return {sw.RealTime(), sw.CpuTime(), bytesRead};
}

Result EvalThroughputMT(const Data &, unsigned)
{
   throw std::runtime_error("Unimplemented");
   return {};
}

Result EvalThroughput(const Data &d, unsigned nThreads)
{
   // TODO sanity checks on d
   if (gDebug > 0) {
      // TODO print what branches we are going to read etc.
   }

   return nThreads > 0 ? EvalThroughputMT(d, nThreads) : EvalThroughputST(d);
}

void PrintThroughput(const Result &r)
{
   std::cout << "Real time: " << r.fRealTime << " s\n";
   std::cout << "CPU time: " << r.fCpuTime << " s\n";
   std::cout << "Uncompressed data read: " << r.fUncompressedBytesRead << " bytes\n";
   std::cout << "Throughput: " << r.fUncompressedBytesRead / r.fRealTime / 1024 / 1024 << " MB/s\n";
}

void Test()
{
   auto writeFile = [](const std::string &fname, int val) {
      TFile f(fname.c_str(), "recreate");
      TTree t("t", "t");
      t.Branch("x", &val);
      for (int i = 0; i < 10000000; ++i)
         t.Fill();
      t.Write();
   };
   writeFile("test1.root", 42);
   writeFile("test2.root", 84);

   const auto result = EvalThroughput({{"t"}, {"test1.root", "test2.root"}, {"x"}}, 0);
   PrintThroughput(result);
}

int main()
{
   Test();
   return 0;
}
