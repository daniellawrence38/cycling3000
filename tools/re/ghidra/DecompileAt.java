// DecompileAt.java: decompile the functions containing the given addresses and say who calls them.
//
// Run through tools/re/gh.sh:   gh.sh DecompileAt.java 0x4222137c 0x4205062c
// An argument may also be a function name. Output goes to stdout between BEGIN/END markers, so a
// caller can cut it out of Ghidra's own logging.
//
// Read-only: it changes nothing in the Ghidra program except, when an address is in code that
// auto-analysis did not make a function of, to define that function so it can be decompiled; the
// project is opened with -readOnly so even that is not saved.
//@category CC700

import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: DecompileAt.java <address-or-name> ...");
            return;
        }
        DecompInterface dec = new DecompInterface();
        dec.toggleCCode(true);
        dec.toggleSyntaxTree(false);
        dec.setSimplificationStyle("decompile");
        if (!dec.openProgram(currentProgram)) {
            println("decompiler did not open: " + dec.getLastMessage());
            return;
        }
        FunctionManager fm = currentProgram.getFunctionManager();
        StringBuilder out = new StringBuilder();
        for (String arg : args) {
            Function f = null;
            Address a = null;
            if (arg.startsWith("0x") || arg.startsWith("0X")) {
                a = toAddr(Long.parseLong(arg.substring(2), 16));
                f = fm.getFunctionContaining(a);
                if (f == null) {
                    // Not a function yet: walk back to the nearest Xtensa ENTRY (xx 41 36 / 36 x1 00 form
                    // is not relied on; Ghidra is simply asked to disassemble and make a function here).
                    disassemble(a);
                    f = createFunction(a, null);
                }
            } else {
                List<Function> named = getGlobalFunctions(arg);
                if (!named.isEmpty())
                    f = named.get(0);
            }
            out.append("===== BEGIN ").append(arg).append("\n");
            if (f == null) {
                out.append("no function at or named ").append(arg).append("\n");
                out.append("===== END ").append(arg).append("\n");
                continue;
            }
            out.append("function ").append(f.getName()).append(" entry ").append(f.getEntryPoint())
               .append(" body ").append(f.getBody().getMinAddress()).append("..")
               .append(f.getBody().getMaxAddress()).append(" (").append(f.getBody().getNumAddresses())
               .append(" bytes)\n");

            TreeSet<String> callers = new TreeSet<>();
            ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
            while (it.hasNext()) {
                Reference r = it.next();
                Function from = fm.getFunctionContaining(r.getFromAddress());
                callers.add(r.getFromAddress() + " " + r.getReferenceType().getName() + " in "
                    + (from == null ? "(no function)" : from.getName() + "@" + from.getEntryPoint()));
            }
            out.append("referenced from ").append(callers.size()).append(" place(s):\n");
            int shown = 0;
            for (String c : callers) {
                if (shown++ == 40) {
                    out.append("  ... ").append(callers.size() - 40).append(" more\n");
                    break;
                }
                out.append("  ").append(c).append("\n");
            }
            List<String> callees = new ArrayList<>();
            for (Function c : f.getCalledFunctions(monitor))
                callees.add(c.getName() + "@" + c.getEntryPoint());
            out.append("calls: ").append(String.join(", ", callees)).append("\n\n");

            DecompileResults res = dec.decompileFunction(f, 120, monitor);
            if (res != null && res.decompileCompleted())
                out.append(res.getDecompiledFunction().getC());
            else
                out.append("decompile failed: ").append(res == null ? "null" : res.getErrorMessage()).append("\n");
            out.append("===== END ").append(arg).append("\n");
        }
        dec.dispose();
        System.out.println(out);
    }
}
