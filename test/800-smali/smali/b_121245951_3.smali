.class public LB121245951_3;

.super Ljava/lang/Object;

.method public static run(Ljava/lang/Object;)V
  .registers 3

  const/4 v1, 0x1

:LcatchStart

  monitor-enter v2

  # Possibly throwing to merge v1 into catch handler as int.
  sget-object v2, Ljava/lang/System;->out:Ljava/io/PrintStream;

  move-object v1, v2

  # This should cause a runtime failure, and not merge into the
  # catch handler.
  return-void

:LcatchEnd
:LcatchHandler
  move-exception v0
  # If the lock fail at the return-void above merged into the catch
  # handler, this will fail the class.
  add-int/lit8 v1, v1, 0x1
  throw v0

.catchall {:LcatchStart .. :LcatchEnd} :LcatchHandler

.end method