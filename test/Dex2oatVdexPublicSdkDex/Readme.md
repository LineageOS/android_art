This dex file redefines some popular java.lang classes in order to assist
with testing sdk-based verification.

Each of these classes only defines the stubs for a limited number of methods
and fields. They are compiled into a dex file and passed to dex2oat to simulate
a proper SDK-stub jars. The methods not declared here should not be resolved.
