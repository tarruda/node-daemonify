// Based on suckless version: http://st.suckless.org/.
#ifndef ARG_H
#define ARG_H

#define GETOPTS(progname)                                                 \
    for (int brk1_ = 0, brk2_; !brk1_; argc &&                            \
         (argv[0][0] == '-' && argv[0][1] == '-' && argv[0][2] == '\0') ? \
         argc--, argv++ : 0)                                              \
    for (char argc_, **argv_; !brk1_; brk1_ = 1)                          \
    for (progname = *argv++, argc--;                                      \
         argc && argv[0] && argv[0][0] == '-' && argv[0][1]               \
           && (argv[0][1] != '-' || argv[0][2] != '\0');                  \
         argv++, argc--)                                                  \
    for (brk2_ = 0, argv[0]++, argv_ = argv;                              \
         argv[0][0] && !brk2_ && argv_ == argv && (argc_ = argv[0][0]);   \
         argv[0]++)                                                       \
    switch (argc_)

#define OPTARG                                                            \
  ((argv[0][1] == '\0' && argv[1] == NULL) ?                              \
    ((usage(progname)), abort(), (char *)0):                              \
    (brk2_ = 1, (argv[0][1] != '\0')       ?                              \
      (&argv[0][1])                        :                              \
      (argc--, argv++, argv[0])))

#endif  // ARG_H
