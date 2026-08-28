// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
#ifdef LAB_LOCK
  int nts;
  int n;
#endif
};

#ifdef LAB_LOCK
// Reader-writer lock.
struct rwspinlock {
  struct spinlock l;    // protects the fields below
  int nreader;          // number of readers currently holding the lock
  int writer;           // 1 if a writer currently holds the lock
  int waiting_writer;   // number of writers waiting to acquire
};
#endif
