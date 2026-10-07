# Ember demo project script.
#
# Run with:  EmberEditor scripts/demo.cmd
#
# Each line is one command, and the whole file runs in order. This builds a
# project, writes the scene that exercises every engine feature into it, runs
# the result for two seconds and exports a shippable build.
#
# Lines beginning with # are comments. Arguments containing spaces may be quoted.

# Create the project. The first argument is the directory, the second its name.
create /tmp/emberdemo MyGame

# Write the scene that uses every built-in component, plus the script that
# drives one of its entities. The engine builds it in code rather than shipping
# it as an asset, so a change that breaks it fails the tests.
write-test-scene

# Load it into the running application and step it for two seconds.
load-scene test.ember
run 120

# Report what the last frame cost.
statistics

# Export a build that EmberRuntime can play.
export /tmp/emberdemo/dist