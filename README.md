# Setting password

    ./genpwhash > ~/.cantera/lock-passkey

# This program locks X11 screens

## Why NOT use this program:

 * Requires that the user can read their own password hash through getpwnam(),
   or is willing to put a crypt() hash in ~/.cantera/lock-passkey.

## Why use this program:

 * You want a separate password for screen unlocking.  You may want this if ...

   * ... you often lock your screen to go get a beverage, and return to your
     computer holding a beverage in your right hand.  In this case, it may be
     convenient to have an unlock password you can easily type with just your
     left hand.

   * ... you participate in video conferences, or may otherwise be filmed
     while typing your unlock password.  Replicating password from video
     footage is easy unless you type faster than the video frame rate, but if
     your unlock password is different from your SSH password, it cannot be
     used for remote logins.

 * You don't want to press enter when you've finished typing your password.  If
   your monitor is slow to turn on, you will never accidentally send your
   password as a chat message with this locking program.

 * You don't want to be penalized for typos by libpam, because you know that
   it's extremely unrealistic that anyone will successfully brute force attack
   your password with a keyboard.

 * You want to see for how long the screen has been locked, so that you can
   easily compare the time cost of various lunch strategies, and so that others
   can see for how long you've been away.

 * You want to see the clock without unlocking.
