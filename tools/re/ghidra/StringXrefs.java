// StringXrefs.java: find every place the code uses a string, by its text.
//
// Run through tools/re/gh.sh:   gh.sh StringXrefs.java "SendPowerOffCmd" "Uart Read Data Result"
//
// This is the project's "find a string, find the l32r that loads it" method (docs/BRINGUP-LOG.md),
// done by Ghidra's reference database instead of by hand. On Xtensa a string is reached in two hops:
// the instruction (l32r) reads a word from a literal pool, and that word is the string's address. Both
// hops are followed here: direct references to the string, and references to any literal-pool word
// that holds the string's address (found by searching memory for the address bytes, so it does not
// depend on auto-analysis having typed the literal as a pointer).
//
// Read-only.
//@category CC700

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class StringXrefs extends GhidraScript {
    private static final int MAX_HITS = 12;

    private List<Address> findAll(byte[] pat, boolean alignedOnly) throws Exception {
        Memory mem = currentProgram.getMemory();
        List<Address> out = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized())
                continue;
            Address at = b.getStart();
            while (at != null && out.size() < 4096) {
                Address hit = mem.findBytes(at, b.getEnd(), pat, null, true, monitor);
                if (hit == null)
                    break;
                if (!alignedOnly || (hit.getOffset() & 3) == 0)
                    out.add(hit);
                if (hit.equals(b.getEnd()))
                    break;
                at = hit.add(1);
            }
        }
        return out;
    }

    private void refsTo(Address target, String how, TreeSet<String> out) {
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(target);
        while (it.hasNext()) {
            Reference r = it.next();
            Address from = r.getFromAddress();
            if (currentProgram.getListing().getInstructionAt(from) == null)
                continue; // a data-to-data reference is not a use by code
            Function f = fm.getFunctionContaining(from);
            out.add(from + " " + how + " in " + (f == null ? "(no function)" : f.getName() + "@" + f.getEntryPoint()));
        }
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        StringBuilder out = new StringBuilder();
        for (String text : getScriptArgs()) {
            out.append("===== BEGIN ").append(text).append("\n");
            List<Address> hits = findAll(text.getBytes(StandardCharsets.UTF_8), false);
            out.append(hits.size()).append(" occurrence(s) of the text\n");
            int n = 0;
            for (Address hit : hits) {
                if (n++ == MAX_HITS) {
                    out.append("... more occurrences not shown\n");
                    break;
                }
                // Back up to the start of the C string so the reference target is right.
                Address start = hit;
                for (int i = 0; i < 512; i++) {
                    Address prev = start.subtract(1);
                    if (!mem.contains(prev) || mem.getByte(prev) == 0)
                        break;
                    start = prev;
                }
                StringBuilder s = new StringBuilder();
                for (int i = 0; i < 160; i++) {
                    byte c = mem.getByte(start.add(i));
                    if (c == 0)
                        break;
                    s.append(c >= 0x20 && c < 0x7f ? (char) c : '.');
                }
                out.append("string @ ").append(start).append(": \"").append(s).append("\"\n");

                TreeSet<String> uses = new TreeSet<>();
                refsTo(start, "direct", uses);
                long v = start.getOffset();
                byte[] le = {(byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24)};
                for (Address lit : findAll(le, true))
                    refsTo(lit, "via literal " + lit, uses);
                if (uses.isEmpty())
                    out.append("  no code reference found\n");
                for (String u : uses)
                    out.append("  ").append(u).append("\n");
            }
            out.append("===== END ").append(text).append("\n");
        }
        System.out.println(out);
    }
}
