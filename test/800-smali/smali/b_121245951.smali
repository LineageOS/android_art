.class public LB121245951;

.super Ljava/lang/Object;

.method public static run(ZLjava/lang/Object;)V
  .registers 3

  # Create an unequal lock stack.

  if-eqz v1, :LfalseBranch

:LtrueBranch
  monitor-enter v2
  monitor-enter v2
  goto :Ljoin

:LfalseBranch
  monitor-enter v2
  goto :Ljoin

:Ljoin
  monitor-exit v2

  # Should throw here.
  return-void
.end method
