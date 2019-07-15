.class public LB121245951_2;

.super Ljava/lang/Object;

.method public static run(ZLjava/lang/Object;)V
  .registers 3

  # Create an unequal lock stack.

  if-eqz v1, :LfalseBranch

:LtrueBranch
  monitor-enter v2
  monitor-enter v2
  const/4 v0, 0x0
  goto :Ljoin

:LfalseBranch
  monitor-enter v2
  move-object v0, v2
  goto :Ljoin

:Ljoin
  monitor-exit v2

  # This should fail the class
  add-int/lit8 v0, v0, 0x1

  return-void
.end method
