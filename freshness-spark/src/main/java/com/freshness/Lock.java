package com.freshness;

public class Lock {
    boolean locked;

    Lock() {
        locked = true;
    }

    public boolean lock() {
        if (locked) {
            return false;
        } else {
            locked = true;
            return true;
        }
    }

    public void unlock() {
        locked = false;
    }
}
