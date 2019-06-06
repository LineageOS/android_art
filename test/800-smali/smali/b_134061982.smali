.class public LB134061982;
.super Ljava/lang/Object;


.method public constructor <init>()V
.registers 1
       invoke-direct {p0}, Ljava/lang/Object;-><init>()V
       return-void
.end method

.method public static run(I)V
.registers 4

# Registers:
# * v0 = 0/null
# * v1 = "outer" catch value to operate on
# * v2 = exception value for inner catch
# * v3 = p0 = input for two legs.

        const v0, 0

        # Start with r1 == null
        const v1, 0

        if-eqz p0, :direct_leg
        goto :indirect_leg

:direct_leg
        throw v0

:indirect_leg
        # Make r1 not-reference.
        const v1, 1
        throw v0

:end
        return-void

:catch_inner
        move-exception v2
        # r2 should not be primitive, so this should hard-fail if reached.
        add-int/lit8 v2, v2, 0x1
        goto :end

:catch_outer
        # Just some random call.
        invoke-virtual {v1}, Ljava/io/PrintStream;->println()V
        goto :end

# Direct leg is directly covered by catch_outer.
.catchall {:direct_leg .. :indirect_leg} :catch_outer

# Indirect leg is directly covered by catch_inner.
# * Covered by unresolved exception class -> unreachable.
.catch Ldoes/not/ResolveException; {:indirect_leg .. :end} :catch_inner

# catch_inner is covered by catch_outer.
.catchall {:catch_inner .. :catch_outer} :catch_outer

.end method
