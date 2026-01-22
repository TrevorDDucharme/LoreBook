# The Run-First, Debugger-Driven Development Manifesto

## Our Position

We do **not** write automated tests that assert correctness.
Not fewer tests. 
Not optional tests.
**No tests. EVER.**

We reject meaningless abstraction and ceremonial scaffolding that replace thinking with ritual.

We expect developers to:

* Write the code
* Run the code
* Validate behavior
* Debug if necessary
* Fix problems
* Repeat validation
* Ship the patch

Tests, dashboards, and fake metrics do **not** replace **responsibility, attention, or understanding**. Execution is the only reliable proof of correctness.

---

## **The Prime Directive**

**RUN YOUR CODE. USE YOUR FUCKING DEBUGGER.**

If something is wrong:

* Fatal issues produce stack traces.
* Non-fatal issues are discovered by stepping through the code.

If you don’t notice it, you weren’t paying attention. This is not optional. **This is your job**.

---

## **What We Believe**

* Software correctness is **proven by real execution**, not hypothetical assertions.
* Debuggers are superior to test suites.
* Debuggers teach understanding; test suites and logs do not.
* Stepping through code builds understanding. Watching **state change in real time** builds comprehension you cannot fake and beats reading an assertion.
* Developers—not frameworks or ceremonial abstractions—are responsible for correctness.
* If you need a test to tell you something went wrong or is broken, you **weren’t paying attention**.

---

## **How We Work**

Every change follows this loop:

1. Write the code
2. Run the application
3. Observe real behavior

If it crashes:

* Read the stack trace
* Fix the problem

If behavior is wrong:

* Set breakpoints
* Step line by line
* Inspect variables
* Follow the actual execution path
* Understand why it happened
* Fix it

Repeat validation. **Ship the patch.**

**No meaningless abstractions. No simulations. No pretending.**

---

## **Abstraction and What We Accept**

Abstraction is allowed **only if it clarifies reality and reduces repetition**, never hides it. Acceptable uses:

* Templates and compile-time reflection that reduce repetition or enforce correctness
* Abstractions that **enhance understanding, not replace it**
* Carefully applied abstractions that enhance understanding, not replace it
* Reusing existing code is fine—but only **after you fully understand it**

Pollution includes:

* Over-engineered nested namespaces
* Ceremonial scaffolding classes
* Frameworks that hide fundamentals
* Using APIs without understanding the domain
* Type-erased containers by default
* Runtime abstractions that replace reasoning with black-box magic

Mastering an API does **not** mean you understand the system. Stop pretending.

---

## **Debugger-First Policy**

When behavior is unexpected:

* **Do not write a test.**
* **Do not add blind logging.**
* **Do use your debugger.**

  * Set breakpoints
  * Step through code
  * Inspect variables and state
  * Follow execution paths

This fixes the bug **and** improves understanding.
Tests teach nothing. Debugging teaches everything.

---

## **Explicitly Forbidden**

* Unit tests
* Integration tests
* Snapshot tests
* Mock-driven development
* Test coverage metrics
* Test-driven development
* Meaningless abstractions and ceremonial scaffolding

Pull requests containing these will be rejected.

---

## **Accountability, Learning, & Growth**

We demand from every developer on this team:

* **Understanding:** You must know the code you write, the changes you make, and the impact of your actions.
* **Learning from mistakes:** Mistakes are **expected and required**. They are the path to growth, not evidence of failure. Every bug, misstep, or unexpected behavior is an opportunity to gain knowledge.
* **Demonstrated comprehension:** If you make a mistake—**even the same mistake twice**—you will not be fired. But you **must be able to explain**:

  * How the code works
  * What it does
  * Why it broke
* **Shared learning:** If you don’t understand, we step through it together. This is where growth happens.
* **Ask questions—always:** Even “stupid” questions are welcome. If you don’t ask, you don’t learn. Curiosity and humility are required.

**This is not about punishment.** Your job is **not at risk** for making mistakes. Your job **is to learn, to grow, and to become a better developer**. Pretending to understand or putting on a mask is unacceptable.

**Intelligence, curiosity, and desire to grow are mandatory.** Accountability is about knowledge, not fear. Growth is our goal; mistakes and questions are the fuel.

---

## **Refactoring Rule**

If refactoring feels unsafe:

* **You don’t understand the code yet.**

Solution:

* Run the app
* Step through code
* Learn the system

**Not:** write tests to protect ignorance.

---

## **Definition of Done**

A change is done when:

* The application runs
* Behavior is correct in real usage
* The developer personally ran and debugged the code
* No obvious regressions are observed

Automated test results are irrelevant. **Execution and observation are the only validation.**

---

## **Final Statement**

Tests do not make software reliable. **Attention does.**
Debuggers do not slow you down. **Ignorance does.**

**If it runs wrong, you know immediately.**
**If it behaves unexpectedly, you know immediately.**
**Use your debugger. Inspect state. Understand why. Fix it. Ship the patch.**

**Mistakes are expected. Mistakes are required. Ask questions. Step through code. Learn. Grow. Be a better developer.**
