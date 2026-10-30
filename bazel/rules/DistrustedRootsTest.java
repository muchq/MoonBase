import java.io.FileInputStream;
import java.io.InputStream;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

/**
 * Checks that every trust store we ship leaves out the roots named in OpenJDK's eMudhra advisory
 * (https://openjdk.org/emudhra, JDK-8395690).
 *
 * <p>It matches by subject CN, the way the advisory's own CheckCerts.java does, not by the
 * fingerprints DistrustRoots filters on. A re-issued or cross-signed root with the same name and
 * different bytes would get past the filter, and this check catches that.
 *
 * <p>Runs as a plain main and not under JUnit, so it needs nothing from Maven.
 */
public class DistrustedRootsTest {

  private static final String[] DISTRUSTED = {
    "emSign Root CA - G1",
    "emSign Root CA - G2",
    "emSign ECC Root CA - G3",
    "AAA Certificate Services",
  };

  // An empty or truncated store "passes" the distrust check and then fails
  // every handshake.
  private static final int MIN_ROOTS = 100;

  public static void main(String[] args) throws Exception {
    List<String> failures = new ArrayList<>();
    check("pem bundle " + args[0], pemBundle(args[0]), failures);
    check("java truststore " + args[1], javaTrustStore(args[1]), failures);
    failures.forEach(f -> System.err.println("FAIL: " + f));
    System.exit(failures.isEmpty() ? 0 : 1);
  }

  private static void check(String what, List<X509Certificate> certs, List<String> failures) {
    if (certs.size() < MIN_ROOTS) {
      failures.add(what + ": only " + certs.size() + " roots");
    }
    for (X509Certificate cert : certs) {
      String subject = cert.getSubjectX500Principal().getName();
      for (String cn : DISTRUSTED) {
        if (subject.contains("CN=" + cn + ",") || subject.endsWith("CN=" + cn)) {
          failures.add(what + ": still trusts " + subject);
        }
      }
    }
    System.out.println("checked " + certs.size() + " roots in " + what);
  }

  private static List<X509Certificate> pemBundle(String path) throws Exception {
    try (InputStream in = new FileInputStream(path)) {
      List<X509Certificate> certs = new ArrayList<>();
      for (Certificate c : CertificateFactory.getInstance("X.509").generateCertificates(in)) {
        certs.add((X509Certificate) c);
      }
      return certs;
    }
  }

  // Read through JSSE with only javax.net.ssl.trustStore set, as the java
  // image entrypoint sets it. If no password is set, the store has to load
  // without one.
  private static List<X509Certificate> javaTrustStore(String path) throws Exception {
    System.setProperty("javax.net.ssl.trustStore", path);
    TrustManagerFactory tmf =
        TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
    tmf.init((KeyStore) null);
    List<X509Certificate> certs = new ArrayList<>();
    for (TrustManager tm : tmf.getTrustManagers()) {
      if (tm instanceof X509TrustManager x509) {
        certs.addAll(List.of(x509.getAcceptedIssuers()));
      }
    }
    return certs;
  }
}
