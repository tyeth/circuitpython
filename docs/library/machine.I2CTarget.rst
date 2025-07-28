.. currentmodule:: machine
.. _machine.I2CTarget:

class I2CTarget -- a device over I2C
====================================

An I2C target is a device which connects to an I2C bus and is controlled by an
I2C controller.  I2C targets can take many forms.  The :class:`machine.I2CTarget`
class implements an I2C target which allows reading and writing to a specific
part of the target's internal memory.

Example usage::

    from machine import I2CTarget

    # Create the backing memory for the I2C target.
    mem = bytearray(8)

    # Create an I2C target.  Depending on the port, extra parameters
    # may be required to select the peripheral and/or pins to use.
    i2c = I2CTargetMemory(addr=67, mem=mem)

    # At this point an I2C controller can read and write `mem`.
    ...

    # Deinitialise the I2C target.
    i2c.deinit()

Constructors
------------

.. class:: I2CTarget(id, addr, *, addrsize=7, mem=None, mem_addrsize=8, scl=None, sda=None)

   Construct and return a new I2CTarget object using the following parameters:

      - *id* identifies a particular I2C peripheral.  Allowed values for
        depend on the particular port/board.
      - *addr* is the I2C address of the target.
      - *addrsize* is the number of bits in the I2C target address.
      - *mem* is an object with the buffer protocol that is writable.
      - *mem_addrsize* is the number of bits in the memory address.
      - *scl* should be a pin object specifying the pin to use for SCL.
      - *sda* should be a pin object specifying the pin to use for SDA.

   Note that some ports/boards will have default values of *scl* and *sda*
   that can be changed in this constructor.  Others will have fixed values
   of *scl* and *sda* that cannot be changed.

General Methods
---------------

.. method:: I2CTarget.deinit()

   Deinitialise the I2C target.  It will no longer respond to requests on the I2C
   bus after this method is called.

.. method:: I2CTarget.readinto(buf)

   Read bytes into the given buffer.  Returns the number of bytes read.

.. method:: I2CTarget.write(buf)

   Write out the bytes from the buffer.  Returns the number of bytes written.

.. method:: I2CTarget.memaddr()

   Get the last memory address that was selected by the controller.  Returns an
   integer.

.. method:: I2CTarget.irq(handler=None, trigger=IRQ_END_READ|IRQ_END_WRITE, hard=False)

   Configure an IRQ *handler* to be called when an event occurs.  The possible events are
   given by the following constants, which can be or'd together and passed to the *trigger*
   argument:

      - ``IRQ_ADDR_MATCH_READ`` indicates that the target was addressed by a
        controller for a read transaction.
      - ``IRQ_ADDR_MATCH_READ`` indicates that the target was addressed by a
        controller for a write transaction.
      - ``IRQ_READ_REQ`` indicates that the controller is requesting data, and this
        request must be satisfied by calling `I2CTarget.write` with the data to be
        passed back to the controller.
      - ``IRQ_WRITE_REQ`` indicates that the controller has written data, and the
        data must be read by calling `I2CTarget.readinto`.
      - ``IRQ_END_READ`` indicates that the controller has finished a read transaction.
      - ``IRQ_END_WRITE`` indicates that the controller has finished a write transaction.

   Not all triggers are available on all ports.  If a port has the constant then that
   event is available.

   Note the following restrictions:

      - ``IRQ_ADDR_MATCH_READ``, ``IRQ_ADDR_MATCH_READ``, ``IRQ_READ_REQ`` and
        ``IRQ_WRITE_REQ`` must be handled by a hard IRQ callback (with the *hard* argument
        set to ``True``).  This is because these events have very strict timing requirements
        and must usually be satisfied synchronously with the hardware event.

      - ``IRQ_END_READ`` and ``IRQ_END_WRITE`` may be handled by either a soft or hard
        IRQ callback (although note that all events must be registered with the same handler,
        so if any events need a hard callback then all events must be hard).

      - If a memory buffer has been supplied in the constructor then ``IRQ_END_WRITE``
        is not emitted for the transaction that writes the memory address.  This is to
        allow ``IRQ_END_READ`` and ``IRQ_END_WRITE`` to function correctly as soft IRQ
        callbacks, where the IRQ handler may be called quite some time after the actual
        hardware event.

Constants
---------

.. data:: I2CTarget.IRQ_ADDR_MATCH_READ
.. data:: I2CTarget.IRQ_ADDR_MATCH_WRITE
          I2CTarget.IRQ_READ_REQ
          I2CTarget.IRQ_WRITE_REQ
          I2CTarget.IRQ_END_READ
          I2CTarget.IRQ_END_WRITE

    IRQ trigger sources.
