/****************************************************************************
 * drivers/ioexpander/gpio_lower_half.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/ioexpander/ioexpander.h>
#include <nuttx/ioexpander/gpio.h>

#ifdef CONFIG_GPIO_LOWER_HALF

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* GPIO lower half driver state */

struct gplh_dev_s
{
  /* Publicly visible lower-half state */

  struct gpio_dev_s gpio;

  /* Private lower half data follows */

  uint8_t pin;                      /* I/O expander pin ID */
  FAR struct ioexpander_dev_s *ioe; /* Contain I/O expander interface */
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  FAR void *handle;                 /* Interrupt attach handle */
  pin_interrupt_t callback;         /* Interrupt callback */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int gplh_handler(FAR struct ioexpander_dev_s *ioe,
                        ioe_pinset_t pinset, FAR void *arg);
#endif

/* GPIO lower half interface methods */

static int gplh_read(FAR struct gpio_dev_s *gpio, FAR bool *value);
static int gplh_write(FAR struct gpio_dev_s *gpio, bool value);
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int gplh_attach(FAR struct gpio_dev_s *gpio,
                       pin_interrupt_t callback);
static int gplh_enable(FAR struct gpio_dev_s *gpio, bool enable);
#endif
static int gplh_setpintype(FAR struct gpio_dev_s *gpio,
                           enum gpio_pintype_e pintype);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* GPIO Lower Half interface operations */

static const struct gpio_operations_s g_gplh_ops =
{
  gplh_read,   /* read   */
  gplh_write,  /* write  */
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  gplh_attach, /* attach */
  gplh_enable, /* enable */
#else
  NULL,        /* attach */
  NULL,        /* enable */
#endif
  gplh_setpintype,
};

/* Identifies the type of the GPIO pin */

static const uint32_t g_gplh_inttype[GPIO_NPINTYPES] =
{
  IOEXPANDER_VAL_DISABLE,         /* GPIO_INPUT_PIN */
  IOEXPANDER_VAL_DISABLE,         /* GPIO_INPUT_PIN_PULLUP */
  IOEXPANDER_VAL_DISABLE,         /* GPIO_INPUT_PIN_PULLDOWN */
  IOEXPANDER_VAL_DISABLE,         /* GPIO_OUTPUT_PIN */
  IOEXPANDER_VAL_DISABLE,         /* GPIO_OUTPUT_PIN_OPENDRAIN */
  CONFIG_GPIO_LOWER_HALF_INTTYPE, /* GPIO_INTERRUPT_PIN */
  IOEXPANDER_VAL_HIGH,            /* GPIO_INTERRUPT_HIGH_PIN */
  IOEXPANDER_VAL_LOW,             /* GPIO_INTERRUPT_LOW_PIN */
  IOEXPANDER_VAL_RISING,          /* GPIO_INTERRUPT_RISING_PIN */
  IOEXPANDER_VAL_FALLING,         /* GPIO_INTERRUPT_FALLING_PIN */
  IOEXPANDER_VAL_BOTH,            /* GPIO_INTERRUPT_BOTH_PIN */
  CONFIG_GPIO_LOWER_HALF_INTTYPE, /* GPIO_INTERRUPT_PIN_WAKEUP */
  IOEXPANDER_VAL_HIGH,            /* GPIO_INTERRUPT_HIGH_PIN_WAKEUP */
  IOEXPANDER_VAL_LOW,             /* GPIO_INTERRUPT_LOW_PIN_WAKEUP */
  IOEXPANDER_VAL_RISING,          /* GPIO_INTERRUPT_RISING_PIN_WAKEUP */
  IOEXPANDER_VAL_FALLING,         /* GPIO_INTERRUPT_FALLING_PIN_WAKEUP */
  IOEXPANDER_VAL_BOTH,            /* GPIO_INTERRUPT_BOTH_PIN_WAKEUP */
};

static const uint32_t g_gplh_wakeuptype[GPIO_NPINTYPES] =
{
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INPUT_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INPUT_PIN_PULLUP */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INPUT_PIN_PULLDOWN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_OUTPUT_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_OUTPUT_PIN_OPENDRAIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_HIGH_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_LOW_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_RISING_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_FALLING_PIN */
  IOEXPANDER_WAKEUP_DISABLE,         /* GPIO_INTERRUPT_BOTH_PIN */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_PIN_WAKEUP */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_HIGH_PIN_WAKEUP */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_LOW_PIN_WAKEUP */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_RISING_PIN_WAKEUP */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_FALLING_PIN_WAKEUP */
  IOEXPANDER_WAKEUP_ENABLE,          /* GPIO_INTERRUPT_BOTH_PIN_WAKEUP */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gplh_handler
 *
 * Description:
 *   I/O expander interrupt callback function.
 *
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int gplh_handler(FAR struct ioexpander_dev_s *ioe,
                        ioe_pinset_t pinset, FAR void *arg)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)arg;

  DEBUGASSERT(priv != NULL && priv->callback != NULL);

  gpioinfo("pin%u: pinset: %lx callback=%p\n",
           priv->pin, (unsigned long)pinset, priv->callback);

  /* We received the callback from the I/O expander, forward this to the
   * upper half GPIO driver via its callback.
   */

  return priv->callback(&priv->gpio, priv->pin);
}
#endif

/****************************************************************************
 * Name: gplh_read
 *
 * Description:
 *   Read the value of the I/O expander pin.
 *
 ****************************************************************************/

static int gplh_read(FAR struct gpio_dev_s *gpio, FAR bool *value)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL && value != NULL);

  gpioinfo("pin%u: value=%p\n", priv->pin, value);

  /* Return the value from the I/O expander */

  return IOEXP_READPIN(priv->ioe, priv->pin, value);
}

/****************************************************************************
 * Name: gplh_write
 *
 * Description:
 *   Set the value of an I/O expander output pin
 *
 ****************************************************************************/

static int gplh_write(FAR struct gpio_dev_s *gpio, bool value)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: value=%u\n", priv->pin, value);

  /* Write the value using the I/O expander */

  return IOEXP_WRITEPIN(priv->ioe, priv->pin, value);
}

/****************************************************************************
 * Name: gplh_attach
 *
 * Description:
 *   Detach and disable any current interrupt on the pin.  Save the callback
 *   information for use when the pin interrupt is enabled.
 *
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int gplh_attach(FAR struct gpio_dev_s *gpio, pin_interrupt_t callback)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: callback=%p\n", priv->pin, callback);

  /* Detach and disable any current interrupt on the pin. */

  if (priv->handle != NULL)
    {
      gpioinfo("pin%u: Detaching handle %p\n", priv->pin, priv->handle);
      IOEP_DETACH(priv->ioe, priv->handle);
      priv->handle = NULL;
    }

  /* Save the callback function pointer for use when the pin interrupt
   * is enabled.
   */

  priv->callback = callback;
  return OK;
}
#endif

/****************************************************************************
 * Name: gplh_enable
 *
 * Description:
 *   Enable or disable the I/O expander pin interrupt
 *
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int gplh_enable(FAR struct gpio_dev_s *gpio, bool enable)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;
  int ret = OK;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: %s callback=%p handle=%p\n",
           priv->pin, enable ? "Enabling" : "Disabling",
           priv->callback, priv->handle);

  /* Are we enabling or disabling the pin interrupt? */

  if (enable)
    {
      /* We are enabling the pin interrupt.  Make certain that there is
       * an interrupt handler already attached.
       */

      if (priv->callback == NULL)
        {
          /* No callback has been attached */

          gpiowarn("WARNING: pin%u: Attempt to enable before attaching\n",
                   priv->pin);
          ret = -EPERM;
        }

      /* Check if the interrupt is already attached and enabled */

      else if (priv->handle == NULL)
        {
#if CONFIG_IOEXPANDER_NPINS <= 64
          ioe_pinset_t pinset = ((ioe_pinset_t)1 << priv->pin);
#else
          ioe_pinset_t pinset = ((ioe_pinset_t)priv->pin);
#endif

          /* We have a callback and the callback is not yet attached.
           * do it now.
           */

          gpioinfo("pin%u: Attaching %p\n", priv->pin, priv->callback);

          priv->handle = IOEP_ATTACH(priv->ioe, pinset, gplh_handler, priv);
          if (priv->handle == NULL)
            {
              gpioerr("ERROR: pin%u: IOEP_ATTACH() failed\n", priv->pin);
              ret = -EIO;
            }
        }
    }
  else
    {
      /* Check if we are already detached */

      if (priv->handle == NULL)
        {
          gpiowarn("WARNING: pin%u: Already detached\n", priv->pin);
        }
      else
        {
          gpioinfo("pin%u: Detaching handle=%p\n", priv->pin, priv->handle);
          ret = IOEP_DETACH(priv->ioe, priv->handle);
          if (ret < 0)
            {
              gpioerr("ERROR: pin%u: IOEP_DETACH() failed %d\n",
                      priv->pin, ret);
            }

          /* We are no longer attached */

          priv->handle = NULL;
        }
    }

  return ret;
}
#endif

/****************************************************************************
 * Name: gplh_setpintype
 *
 * Description:
 *   Set I/O expander pin to an appointed gpiopintype
 *
 ****************************************************************************/

static int gplh_setpintype(FAR struct gpio_dev_s *gpio,
                           enum gpio_pintype_e pintype)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;
  FAR struct ioexpander_dev_s *ioe = priv->ioe;
  uint8_t pin = priv->pin;

  if (pintype >= GPIO_NPINTYPES)
    {
      return -EINVAL;
    }
  else if (pintype == GPIO_OUTPUT_PIN)
    {
      IOEXP_SETDIRECTION(ioe, pin, IOEXPANDER_DIRECTION_OUT);
    }
  else if (pintype == GPIO_OUTPUT_PIN_OPENDRAIN)
    {
      IOEXP_SETDIRECTION(ioe, pin, IOEXPANDER_DIRECTION_OUT_OPENDRAIN);
    }
  else
    {
      if (pintype == GPIO_INPUT_PIN)
        {
          IOEXP_SETDIRECTION(ioe, pin, IOEXPANDER_DIRECTION_IN);
        }
      else if (pintype == GPIO_INPUT_PIN_PULLUP)
        {
          IOEXP_SETDIRECTION(ioe, pin, IOEXPANDER_DIRECTION_IN_PULLUP);
        }
      else
        {
          IOEXP_SETDIRECTION(ioe, pin, IOEXPANDER_DIRECTION_IN_PULLDOWN);
        }

      IOEXP_SETOPTION(ioe, pin, IOEXPANDER_OPTION_INTCFG,
                      (FAR void *)(uintptr_t)g_gplh_inttype[pintype]);

      IOEXP_SETOPTION(ioe, pin, IOEXPANDER_OPTION_WAKEUPCFG,
                      (FAR void *)(uintptr_t)g_gplh_wakeuptype[pintype]);
    }

  gpio->gp_pintype = pintype;
  return OK;
}

/****************************************************************************
 * Name: gpio_lower_half_internal
 *
 * Description:
 *   Internal handler for gpio_lower_half and gpio_lower_half_byname
 *   functions. Initializes gplh_dev_s structure and sets pin type.
 *
 ****************************************************************************/

static FAR struct gplh_dev_s *
gpio_lower_half_internal(FAR struct ioexpander_dev_s *ioe,
                         unsigned int pin,
                         enum gpio_pintype_e pintype)
{
  FAR struct gplh_dev_s *priv;
  FAR struct gpio_dev_s *gpio;
  int ret;

  DEBUGASSERT(ioe != NULL && pin < CONFIG_IOEXPANDER_NPINS &&
              (unsigned int)pintype < GPIO_NPINTYPES);

#ifndef CONFIG_IOEXPANDER_INT_ENABLE
  /* If there is no I/O expander interrupt support, then we cannot handle
   * interrupting pin types.
   */

  DEBUGASSERT(pintype < GPIO_INTERRUPT_PIN);
#endif

  /* Allocate an new instance of the GPIO lower half driver */

  priv = kmm_zalloc(sizeof(struct gplh_dev_s));
  if (priv == NULL)
    {
      gpioerr("ERROR: Failed to allocate driver state %d\n", -ENOMEM);
      return NULL;
    }

  /* Initialize the non-zero elements of the newly allocated instance */

  priv->pin        = (uint8_t)pin;
  priv->ioe        = ioe;
  gpio             = &priv->gpio;
  gpio->gp_ops     = &g_gplh_ops;

  /* Set pintype */

  ret = gplh_setpintype(gpio, pintype);
  if (ret < 0)
    {
      gpioerr("ERROR: gplh_setpintype() failed: %d\n", ret);
      kmm_free(priv);
      return NULL;
    }

  return priv;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpio_lower_half_byname
 *
 * Description:
 *   Create a GPIO pin device driver instance for an I/O expander pin.
 *   The I/O expander pin must have already been configured by the caller
 *   for the particular pintype.
 *
 * Input Parameters:
 *   ioe     - An instance of the I/O expander interface
 *   pin     - The I/O expander pin number for the driver
 *   pintype - See enum gpio_pintype_e
 *   name    - gpio device name
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int gpio_lower_half_byname(FAR struct ioexpander_dev_s *ioe,
                           unsigned int pin, enum gpio_pintype_e pintype,
                           FAR char *name)
{
  FAR struct gplh_dev_s *priv;
  FAR struct gpio_dev_s *gpio;
  int ret;

  DEBUGASSERT(name != NULL);

  /* Initialize gplh_dev_s structure and set pin type */

  priv = gpio_lower_half_internal(ioe, pin, pintype);
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  gpio         = &priv->gpio;
  gpio->gp_ops = &g_gplh_ops;

  /* Register GPIO device by name */

  ret = gpio_pin_register_byname(gpio, name);
  if (ret < 0)
    {
      gpioerr("ERROR: gpio_pin_register_byname() failed: %d\n", ret);
      kmm_free(priv);
    }

  return ret;
}

/****************************************************************************
 * Name: gpio_lower_half
 *
 * Description:
 *   Create a GPIO pin device driver instance for an I/O expander pin.
 *   The I/O expander pin must have already been configured by the caller
 *   for the particular pintype.
 *
 * Input Parameters:
 *   ioe     - An instance of the I/O expander interface
 *   pin     - The I/O expander pin number for the driver
 *   pintype - See enum gpio_pintype_e
 *   minor   - The minor device number to use when registering the device,
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int gpio_lower_half(FAR struct ioexpander_dev_s *ioe, unsigned int pin,
                    enum gpio_pintype_e pintype, int minor)
{
  FAR struct gplh_dev_s *priv;
  FAR struct gpio_dev_s *gpio;
  int ret;

  /* Initialize gplh_dev_s structure and set pin type */

  priv = gpio_lower_half_internal(ioe, pin, pintype);
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  gpio         = &priv->gpio;
  gpio->gp_ops = &g_gplh_ops;

  /* Register the GPIO driver */

  ret = gpio_pin_register(gpio, minor);
  if (ret < 0)
    {
      gpioerr("ERROR: gpio_pin_register() failed: %d\n", ret);
      kmm_free(priv);
    }

  return ret;
}

#endif /* CONFIG_GPIO_LOWER_HALF */
