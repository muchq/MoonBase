import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.HexFormat;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Removes distrusted roots from a PEM bundle, then writes the roots that are left both as a PEM
 * bundle and as a Java truststore.
 *
 * <p>Usage: DistrustRoots IN.pem OUT.pem OUT.jks
 *
 * <p>The certificates kept in the PEM output are byte-for-byte the input blocks. The truststore is
 * JKS on purpose. JSSE loads javax.net.ssl.trustStore with a null password unless one is
 * configured. A JKS store with trusted entries opens fine that way, and the JDK's default PKCS12
 * keystore type reads JKS files too. A PKCS12 store saved with a password encrypts its certs, so it
 * would load empty, and every handshake would fail.
 *
 * <p>The JKS is written here, not by KeyStore.store, which stamps each entry with the wall clock.
 * Every java image carries this store, so its bytes have to be reproducible.
 */
public class DistrustRoots {

  // SHA-256 of the roots that OpenJDK's eMudhra advisory removes
  // (https://openjdk.org/emudhra, JDK-8395690).
  private static final Set<String> DISTRUSTED =
      Set.of(
          // emSign Root CA - G1
          "40F6AF0346A99AA1CD1D555A4E9CCE62C7F9634603EE406615833DC8C8D00367",
          // emSign Root CA - G2
          "1AA0C2709E831BD6E3B5129A00BA41F7EEEF020872F1E6504BF0F6C3F24F3AF3",
          // emSign ECC Root CA - G3
          "86A1ECBA089C4A8D3BBE2734C612BA341D813E043CF9E8A862CD5C57A36BBE6B",
          // AAA Certificate Services
          "D7A7A0FB5D7E2731D771E9484EBCDEF71D5F0C3E0A2948782BC83EE0EA699EF4");

  private static final Pattern PEM =
      Pattern.compile("-----BEGIN CERTIFICATE-----.+?-----END CERTIFICATE-----\n?", Pattern.DOTALL);

  public static void main(String[] args) throws Exception {
    String in = Files.readString(Path.of(args[0]), StandardCharsets.US_ASCII);
    CertificateFactory cf = CertificateFactory.getInstance("X.509");
    MessageDigest sha256 = MessageDigest.getInstance("SHA-256");
    Map<String, byte[]> kept = new TreeMap<>();
    StringBuilder pem = new StringBuilder();

    Matcher m = PEM.matcher(in);
    while (m.find()) {
      String block = m.group();
      X509Certificate cert =
          (X509Certificate)
              cf.generateCertificate(
                  new ByteArrayInputStream(block.getBytes(StandardCharsets.US_ASCII)));
      String fp = HexFormat.of().withUpperCase().formatHex(sha256.digest(cert.getEncoded()));
      if (DISTRUSTED.contains(fp)) {
        continue;
      }
      pem.append(block.endsWith("\n") ? block : block + "\n");
      kept.put(fp.toLowerCase(), cert.getEncoded());
    }

    Files.writeString(Path.of(args[1]), pem, StandardCharsets.US_ASCII);
    Files.write(Path.of(args[2]), jks(kept, "changeit"));
  }

  /** sun.security.provider.JavaKeyStore's v2 format, holding trusted certificate entries only. */
  static byte[] jks(Map<String, byte[]> certsByAlias, String password)
      throws IOException, NoSuchAlgorithmException {
    ByteArrayOutputStream body = new ByteArrayOutputStream();
    DataOutputStream out = new DataOutputStream(body);
    out.writeInt(0xFEEDFEED);
    out.writeInt(2);
    out.writeInt(certsByAlias.size());
    for (Map.Entry<String, byte[]> e : certsByAlias.entrySet()) {
      out.writeInt(2); // trusted certificate entry
      out.writeUTF(e.getKey());
      out.writeLong(0L); // creation date, fixed so the bytes are reproducible
      out.writeUTF("X.509");
      out.writeInt(e.getValue().length);
      out.write(e.getValue());
    }

    MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
    for (char c : password.toCharArray()) {
      sha1.update((byte) (c >> 8));
      sha1.update((byte) c);
    }
    sha1.update("Mighty Aphrodite".getBytes(StandardCharsets.UTF_8));
    sha1.update(body.toByteArray());
    out.write(sha1.digest());
    return body.toByteArray();
  }
}
