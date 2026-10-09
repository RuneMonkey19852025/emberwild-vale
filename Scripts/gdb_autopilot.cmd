set pagination off
set confirm off
set print thread-events off
run
echo \n\n======== SIGSEGV BACKTRACE ========\n
thread apply all bt 60
echo \n======== END ========\n
quit
